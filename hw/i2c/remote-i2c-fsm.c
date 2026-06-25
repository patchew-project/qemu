// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Remote I2C Master Finite State Machine (FSM)
 *
 * This module implements a non-blocking, asynchronous Finite State Machine
 * (FSM) for driving a QEMU I2C master. It bridges a remote backend (which
 * issues transactions) with QEMU's internal I2C bus architecture.
 *
 * The FSM ensures that QEMU's main loop is not blocked during lengthy I2C
 * transactions or when communicating with asynchronous I2C slave devices
 * (which require simulated clock stretching).
 *
 * Execution Model:
 * ----------------
 * The state machine is driven by a QEMU Bottom Half (BH) loop
 * (`remote_i2c_fsm_bh`).
 *
 * 1. Dispatch: The backend initiates a transaction via
 *    `remote_i2c_fsm_dispatch` with `REMOTE_I2C_CMD_START_TX`.
 * 2. Execution: The FSM processes a single byte or address frame, updates its
 *    internal state, and then either re-schedules the BH immediately (for
 *    synchronous devices) or sets a timer to wake up later.
 * 3. Completion: Once the transaction finishes or errors out, control is
 *    returned to the backend via `on_tx_complete` or `on_tx_error` callbacks.
 *
 * State Lifecycle:
 * ----------------
 * - I2C_BUS_IDLE     : Resting state. Awaits backend dispatch. Checks
 *                      for bus busyness and handles retry timers if
 *                      arbitration is lost.
 * - I2C_BUS_ADDR     : Master asserts the bus and sends the slave address. If
 *                      the slave NACKs, the transaction is immediately aborted
 *                      (ENXIO). If ACKed, transitions to SEND, RECV,
 *                      or WAIT_STRETCH.
 * - I2C_BUS_SEND     : Pushes data bytes to the bus. Loops synchronously or
 *                      yields asynchronously per byte.
 * - I2C_BUS_RECV     : Reads data bytes from the bus. Similar yielding behavior
 *                      as SEND.
 * - I2C_BUS_WAIT_STRETCH : Yields execution back to QEMU to simulate clock
 *                      stretching for async slaves or to enforce artificial
 *                      delays (slow_delay_value_ms). Upon timer expiration,
 *                      bounces back to SEND/RECV.
 * - I2C_BUS_END      : Transitional state to guarantee `i2c_end_transfer` is
 *                      called gracefully before finalizing.
 * - I2C_BUS_FINISHED : Cleans up timers, releases the I2C bus, and invokes
 *                      backend callbacks.
 *
 * Asynchronous Support & Clock Stretching:
 * ----------------------------------------
 * When communicating with asynchronous slave devices
 * (`sc->send_async != NULL`), the FSM cannot process the entire transaction
 * buffer in a single pass.
 * Instead, it sends/receives a single byte, transitions to
 * `I2C_BUS_WAIT_STRETCH`, and sets a virtual timer (`s->timer_step`). When
 * the timer fires, the BH loop resumes processing the next byte. This
 * effectively simulates hardware clock stretching without stalling the
 * guest OS.
 *
 * Error Handling:
 * ---------------
 * - NACKs: Immediately abort the transaction and release the bus.
 * - Bus Busy: If arbitration is lost, the module can either error out (EBUSY)
 *   or back off and retry using a cooldown timer.
 * - Timeouts: Monitored during `WAIT_STRETCH` to prevent hung transactions if
 *   a remote slave becomes unresponsive.
 * - Manual Abort: The backend can issue `REMOTE_I2C_CMD_ABORT` or `RESET` to
 *   forcefully release the bus and reset the FSM.
 *
 * Author:
 * Ilya Chichkov <ilya.chichkov.dev@gmail.com>
 *
 */
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties-system.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "block/aio.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "hw/i2c/remote-i2c-cuse.h"
#include "hw/i2c/remote-i2c-backend.h"
#include "qapi/error.h"
#include "trace.h"

#include "hw/i2c/remote-i2c-master.h"

#define I2C_BUS_BUSY_CHECK_TIMER_COOLDOWN_NS 50000

typedef enum i2c_state_result {
    I2C_HANDLER_OK,
    I2C_HANDLER_ERROR,
} i2c_state_result;

static const char *remote_i2c_bus_state_str(int state)
{
    switch (state) {
    case I2C_BUS_IDLE:          return "Idle";
    case I2C_BUS_ADDR:          return "Address";
    case I2C_BUS_SEND:          return "Send";
    case I2C_BUS_RECV:          return "Receive";
    case I2C_BUS_END:           return "End";
    case I2C_BUS_FINISHED:      return "Finished";
    case I2C_BUS_WAIT_STRETCH:  return "WaitStretch";
    default:                    return "Unknown";
    }
}

static
void remote_i2c_fsm_change_bus_state(RemoteI2CMasterState *s, int new_state)
{
    uint16_t old_state = s->backend->bus_state;
    s->backend->bus_state = new_state;

    trace_remote_i2c_master_bus_state_change(
        remote_i2c_bus_state_str(old_state),
        remote_i2c_bus_state_str(new_state)
    );
}

static
void remote_i2c_abort_transaction(RemoteI2CMasterState *s,
                                  int errno_code, const char *reason)
{
    RemoteI2CBackendClass *bc = REMOTE_I2C_BACKEND_GET_CLASS(s->backend);

    timer_del(s->timer);
    timer_del(s->timer_start_transmit);
    timer_del(s->timer_step);

    if (s->backend->bus_state != I2C_BUS_IDLE &&
        s->backend->bus_state != I2C_BUS_FINISHED) {
        i2c_end_transfer(s->bus);
    }

    i2c_bus_release(s->bus);
    remote_i2c_fsm_change_bus_state(s, I2C_BUS_IDLE);

    s->backend->is_transaction_failed = true;
    s->backend->addr_acked = false;
    s->backend->data_acked = false;
    s->backend->timed_out = false;
    s->backend->transaction_index = 0;
    s->backend->waiting_for_async = false;

    trace_remote_i2c_master_abort(errno_code,
                                  s->backend->address,
                                  reason ? reason : "Unknown error");

    if (bc->on_tx_error) {
        bc->on_tx_error(s->backend, errno_code);
    }
}

static i2c_state_result remote_i2c_finish_handler(RemoteI2CMasterState *s)
{
    RemoteI2CBackendClass *bc = REMOTE_I2C_BACKEND_GET_CLASS(s->backend);

    timer_del(s->timer);
    timer_del(s->timer_start_transmit);
    timer_del(s->timer_step);

    if (s->backend->is_transaction_failed) {
        remote_i2c_fsm_change_bus_state(s, I2C_BUS_IDLE);
        i2c_end_transfer(s->bus);
        i2c_bus_release(s->bus);
        if (bc->on_tx_error) {
            bc->on_tx_error(s->backend, EIO);
        }
        return I2C_HANDLER_ERROR;
    }

    remote_i2c_fsm_change_bus_state(s, I2C_BUS_IDLE);

    /*
     * Call on_tx_complete BEFORE ending or releasing the bus.
     * If the backend chains another sub-message via REMOTE_I2C_CMD_NEXT_MSG
     * (repeated start), it sets bus_state back to I2C_BUS_ADDR. In that case
     * we must NOT call i2c_end_transfer (no STOP to the slave) and must NOT
     * release the bus. Only issue STOP + release when the backend is truly done
     * and bus_state remains IDLE.
     */
    if (bc->on_tx_complete) {
        bc->on_tx_complete(s->backend);
    }

    if (s->backend->bus_state == I2C_BUS_IDLE) {
        i2c_end_transfer(s->bus);
        i2c_bus_release(s->bus);
    }

    return I2C_HANDLER_OK;
}

static
void remote_i2c_is_slave_async(RemoteI2CMasterState *s)
{
    I2CNode *node;
    I2CSlave *slave;
    I2CSlaveClass *sc;

    node = QLIST_FIRST(&s->bus->current_devs);
    if (node) {
        slave = node->elt;
        sc = I2C_SLAVE_GET_CLASS(slave);
        s->backend->is_slave_async = (sc->send_async != NULL);
    } else {
        s->backend->is_slave_async = false;
    }
}

static
void remote_i2c_send_ack(RemoteI2CMasterState *s)
{
    i2c_ack(s->bus);
}

static
void remote_i2c_wait_stretch(RemoteI2CMasterState *s)
{
    timer_mod(s->timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + s->backend->timeout_ms);
    remote_i2c_fsm_change_bus_state(s, I2C_BUS_WAIT_STRETCH);
}

static
void remote_i2c_stretch_clk(RemoteI2CMasterState *s, int64_t expire_timer)
{
    timer_mod(s->timer_step,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + expire_timer);
}

static i2c_state_result remote_i2c_addr_handler(RemoteI2CMasterState *s)
{
    bool started_async = true;

    s->backend->transaction_index = 0;
    s->backend->is_transaction_failed = false;
    s->backend->addr_acked = false;
    s->backend->data_acked = false;
    s->backend->timed_out = false;

    if (s->backend->is_recv) {
        trace_remote_i2c_master_i2cdev_receive(s->backend->transaction_length);

        /* i2c_start_recv returns non-zero if the slave NACKs the address */
        if (i2c_start_recv(s->bus, s->backend->address)) {
            goto nack;
        }

        remote_i2c_is_slave_async(s);
        if (s->backend->is_slave_async) {
            remote_i2c_wait_stretch(s);
            return I2C_HANDLER_OK;
        }

        s->backend->addr_acked = true;
        remote_i2c_send_ack(s);
        remote_i2c_fsm_change_bus_state(s, I2C_BUS_RECV);
    } else {
        trace_remote_i2c_master_i2cdev_send(s->backend->transaction_length);

        if (i2c_start_send_async(s->bus, s->backend->address)) {
            /*
             * Fallback to synchronous send.
             * If it still returns non-zero, it's a NACK.
             */
            if (i2c_start_send(s->bus, s->backend->address)) {
                goto nack;
            }
            started_async = false;
        }

        if (started_async) {
            remote_i2c_is_slave_async(s);
        } else {
            s->backend->is_slave_async = false;
        }

        if (s->backend->is_slave_async) {
            remote_i2c_wait_stretch(s);
            return I2C_HANDLER_OK;
        }

        s->backend->addr_acked = true;
        remote_i2c_send_ack(s);
        remote_i2c_fsm_change_bus_state(s, I2C_BUS_SEND);
    }

    return I2C_HANDLER_OK;

nack:
    remote_i2c_abort_transaction(s, ENXIO, "Address NACKed");

    return I2C_HANDLER_ERROR;
}

static i2c_state_result remote_i2c_send_handler(RemoteI2CMasterState *s)
{
    uint8_t data = 0;
    int ret = 0;

    if (s->backend->is_slave_async && s->backend->data_acked) {
        s->backend->data_acked = false;
        s->backend->transaction_index++;
    }

    if (s->backend->transaction_index >= s->backend->transaction_length) {
        remote_i2c_fsm_change_bus_state(s, I2C_BUS_END);
        return I2C_HANDLER_OK;
    }

    if (s->backend->is_slave_async) {
        data = s->backend->transaction_buf[s->backend->transaction_index];
        trace_remote_i2c_master_send_byte(data);

        i2c_send_async(s->bus, data);
        remote_i2c_wait_stretch(s);
        return I2C_HANDLER_OK;
    }

    for (; s->backend->transaction_index < s->backend->transaction_length;
         s->backend->transaction_index++) {
        data = s->backend->transaction_buf[s->backend->transaction_index];
        trace_remote_i2c_master_send_byte(data);

        ret = i2c_send(s->bus, data);
        if (ret != 0) {
            s->backend->is_transaction_failed = true;
            remote_i2c_fsm_change_bus_state(s, I2C_BUS_END);
            return I2C_HANDLER_ERROR;
        }
    }

    remote_i2c_fsm_change_bus_state(s, I2C_BUS_END);
    return I2C_HANDLER_OK;
}

static i2c_state_result remote_i2c_recv_handler(RemoteI2CMasterState *s)
{
    uint8_t buf = 0;

    s->backend->data_acked = false;

    if (s->backend->transaction_index < s->backend->transaction_length) {
        buf = i2c_recv(s->bus);
        trace_remote_i2c_master_recv_byte(buf);

        s->backend->transaction_buf[s->backend->transaction_index] = buf;
        s->backend->transaction_index++;
    }

    if (s->backend->transaction_index >= s->backend->transaction_length) {
        remote_i2c_fsm_change_bus_state(s, I2C_BUS_END);
        return I2C_HANDLER_OK;
    }

    if (s->backend->is_slave_async) {
        remote_i2c_wait_stretch(s);
    } else {
        remote_i2c_send_ack(s);
    }

    return I2C_HANDLER_OK;
}

/*
 * remote_i2c_fsm_bh:
 * @opaque: Dereferenced generic pointer pointing to the RemoteI2CMasterState.
 *
 * Serves as the primary asynchronous Bottom Half (BH) scheduler and state
 * dispatch loop for the frontend I2C hardware machine.
 */
void remote_i2c_fsm_bh(void *opaque)
{
    RemoteI2CMasterState *s = opaque;

    s->backend->waiting_for_async = false;

    switch (s->backend->bus_state) {
    case I2C_BUS_IDLE:
        break;
    case I2C_BUS_ADDR:
        remote_i2c_addr_handler(s);
        break;
    case I2C_BUS_SEND:
        remote_i2c_send_handler(s);
        break;
    case I2C_BUS_RECV:
        remote_i2c_recv_handler(s);
        break;
    case I2C_BUS_END:
        remote_i2c_fsm_change_bus_state(s, I2C_BUS_FINISHED);
        break;
    case I2C_BUS_FINISHED:
        remote_i2c_finish_handler(s);
        break;
    case I2C_BUS_WAIT_STRETCH:
    case I2C_BUS_WAIT_RELEASE:
        if (s->backend->timed_out) {
            remote_i2c_abort_transaction(s, ETIMEDOUT,
                                         "Timed out waiting for slave to "
                                         "release clock stretching");
            return;
        }

        if (!s->delay_completed && s->slow_delay_value_ms > 0) {
            s->delay_completed = true;
            remote_i2c_stretch_clk(s, s->slow_delay_value_ms * 1000000ULL);
            trace_remote_i2c_master_stretch_delay(s->slow_delay_value_ms);
            return;
        }

        s->delay_completed = false;

        timer_del(s->timer);
        timer_del(s->timer_start_transmit);
        timer_del(s->timer_step);

        if (!s->backend->addr_acked) {
            trace_remote_i2c_master_async_ack();
            s->backend->addr_acked = true;
            s->backend->data_acked = s->backend->is_recv;
        } else {
            trace_remote_i2c_master_async_ack();
            s->backend->data_acked = true;
        }

        if (s->backend->is_recv) {
            remote_i2c_fsm_change_bus_state(s, I2C_BUS_RECV);
            remote_i2c_recv_handler(s);
        } else {
            remote_i2c_fsm_change_bus_state(s, I2C_BUS_SEND);
            remote_i2c_send_handler(s);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Remote I2C Master: Invalid bus state %d\n",
                      s->backend->bus_state);
        remote_i2c_abort_transaction(s, EINVAL, "Invalid FSM state");
        break;
    }

    /* Reschedule Logic */
    if (s->backend->bus_state != I2C_BUS_IDLE &&
        s->backend->bus_state != I2C_BUS_WAIT_STRETCH) {
        qemu_bh_schedule(s->bh);
    }
}

static void remote_i2c_bus_start_transmit(RemoteI2CMasterState *s)
{
    RemoteI2CBackendClass *bc = REMOTE_I2C_BACKEND_GET_CLASS(s->backend);

    if (i2c_bus_busy(s->bus)) {
        if (s->raise_arbitrage_lost) {
            if (bc->on_tx_error) {
                bc->on_tx_error(s->backend, EBUSY);
            }
            return;
        } else {
            timer_mod(s->timer_start_transmit,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      I2C_BUS_BUSY_CHECK_TIMER_COOLDOWN_NS);
            return;
        }
    }

    remote_i2c_fsm_change_bus_state(s, I2C_BUS_ADDR);

    i2c_bus_master(s->bus, s->bh);
    i2c_schedule_pending_master(s->bus);
}

void remote_i2c_fsm_dispatch(RemoteI2CMasterState *s, RemoteI2CCommand cmd)
{
    switch (cmd) {
    case REMOTE_I2C_CMD_START_TX:
        remote_i2c_bus_start_transmit(s);
        break;
    case REMOTE_I2C_CMD_NEXT_MSG:
        /*
         * Repeated START: the bus is already held from the previous
         * sub-transaction. Skip bus acquisition and go straight to the
         * address phase so the slave sees Sr (repeated start) not a
         * full STOP+START cycle.
         *
         * Do NOT schedule the BH here. The BH reschedule logic at the
         * bottom of remote_i2c_fsm_bh will schedule it once state=ADDR
         * is visible on the next iteration.
         */
        remote_i2c_fsm_change_bus_state(s, I2C_BUS_ADDR);
        break;
    case REMOTE_I2C_CMD_ABORT:
        remote_i2c_abort_transaction(s, ECANCELED,
                                     "Transaction aborted by backend");
        break;
    case REMOTE_I2C_CMD_RESET:
        if (s->backend->bus_state != I2C_BUS_IDLE &&
            s->backend->bus_state != I2C_BUS_FINISHED) {
            i2c_end_transfer(s->bus);
        }
        i2c_bus_release(s->bus);
        remote_i2c_fsm_change_bus_state(s, I2C_BUS_IDLE);
        s->backend->is_transaction_failed = false;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Remote I2C Master: Invalid command %d\n", cmd);
        break;
    }
}

void remote_i2c_fsm_timer_start_transmit_cb(void *opaque)
{
    remote_i2c_bus_start_transmit(opaque);
}
