/*
 * QTest
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 * Copyright SUSE LINUX Products GmbH 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *  Andreas Färber    <afaerber@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef LIBQTEST_H
#define LIBQTEST_H

#include "qapi/qmp/qdict.h"

typedef struct QTestState QTestState;

/**
 * global_qtest:
 * The current test object.
 *
 * Many functions in this file implicitly operate on the current
 * object; tests that need to alternate between two parallel
 * connections can do so by switching which test state is current
 * before issuing commands.
 */
extern QTestState *global_qtest;

/**
 * qtest_start:
 * @extra_args: other arguments to pass to QEMU.
 *
 * Start QEMU, and complete the QMP handshake. Sets #global_qtest, which
 * is returned for convenience.
 *
 * Returns: #QTestState instance.
 */
QTestState *qtest_start(const char *extra_args);

/**
 * qtest_start_without_qmp_handshake:
 * @extra_args: other arguments to pass to QEMU.
 *
 * Starts the connection, but does no handshakes; sets #global_qtest.
 */
void qtest_start_without_qmp_handshake(const char *extra_args);

/**
 * qtest_quit:
 * @s: #QTestState instance to operate on.
 *
 * Shut down the QEMU process associated to @s.  See also qtest_end()
 * for clearing #global_qtest.
 */
void qtest_quit(QTestState *s);

/**
 * qtest_end:
 *
 * Shut down the current #global_qtest QEMU process.
 */
static inline void qtest_end(void)
{
    qtest_quit(global_qtest);
    global_qtest = NULL;
}

/**
 * qtest_qmp:
 * @s: #QTestState instance to operate on.
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and returns the response.
 */
QDict *qtest_qmp(QTestState *s, const char *fmt, ...);

/**
 * qtest_async_qmp:
 * @s: #QTestState instance to operate on.
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qtest_async_qmp(QTestState *s, const char *fmt, ...);

/**
 * qtest_qmpv:
 * @s: #QTestState instance to operate on.
 * @fmt: QMP message to send to QEMU
 * @ap: QMP message arguments
 *
 * Sends a QMP message to QEMU and returns the response.
 */
QDict *qtest_qmpv(QTestState *s, const char *fmt, va_list ap);

/**
 * qtest_async_qmpv:
 * @s: #QTestState instance to operate on.
 * @fmt: QMP message to send to QEMU
 * @ap: QMP message arguments
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qtest_async_qmpv(QTestState *s, const char *fmt, va_list ap);

/**
 * qmp_receive:
 *
 * Reads a QMP message from QEMU, using #global_qtest, and returns the
 * response.
 */
QDict *qmp_receive(void);

/**
 * qmp_eventwait:
 * @s: #event event to wait for.
 *
 * Continuously polls for QMP responses, using #global_qtest, until it
 * receives the desired event.
 */
void qmp_eventwait(const char *event);

/**
 * qmp_eventwait_ref:
 * @s: #event event to wait for.
 *
 * Continuously polls for QMP responses, using #global_qtest, until it
 * receives the desired event.  Returns a copy of the event for
 * further investigation.
 */
QDict *qmp_eventwait_ref(const char *event);

/**
 * qtest_hmp:
 * @s: #QTestState instance to operate on.
 * @fmt...: HMP command to send to QEMU
 *
 * Send HMP command to QEMU via QMP's human-monitor-command.
 * QMP events are discarded.
 *
 * Returns: the command's output.  The caller should g_free() it.
 */
char *qtest_hmp(QTestState *s, const char *fmt, ...);

/**
 * qtest_hmpv:
 * @s: #QTestState instance to operate on.
 * @fmt: HMP command to send to QEMU
 * @ap: HMP command arguments
 *
 * Send HMP command to QEMU via QMP's human-monitor-command.
 * QMP events are discarded.
 *
 * Returns: the command's output.  The caller should g_free() it.
 */
char *qtest_hmpv(QTestState *s, const char *fmt, va_list ap);

/**
 * get_irq:
 * @num: Interrupt to observe.
 *
 * Returns: The level of the @num interrupt, using #global_qtest.
 */
bool get_irq(int num);

/**
 * irq_intercept_in:
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-in pins of the device
 * whose path is specified by @string, using #global_qtest.
 */
void irq_intercept_in(const char *string);

/**
 * irq_intercept_out:
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-out pins of the device
 * whose path is specified by @string, using #global_qtest.
 */
void irq_intercept_out(const char *string);

/**
 * outb:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write an 8-bit value to an I/O port, using #global_qtest.
 */
void outb(uint16_t addr, uint8_t value);

/**
 * outw:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 16-bit value to an I/O port, using #global_qtest.
 */
void outw(uint16_t addr, uint16_t value);

/**
 * outl:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 32-bit value to an I/O port, using #global_qtest.
 */
void outl(uint16_t addr, uint32_t value);

/**
 * inb:
 * @addr: I/O port to read from.
 *
 * Returns an 8-bit value from an I/O port, using #global_qtest.
 */
uint8_t inb(uint16_t addr);

/**
 * inw:
 * @addr: I/O port to read from.
 *
 * Returns a 16-bit value from an I/O port, using #global_qtest.
 */
uint16_t inw(uint16_t addr);

/**
 * inl:
 * @addr: I/O port to read from.
 *
 * Returns a 32-bit value from an I/O port, using #global_qtest.
 */
uint32_t inl(uint16_t addr);

/**
 * writeb:
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes an 8-bit value to memory, using #global_qtest.
 */
void writeb(uint64_t addr, uint8_t value);

/**
 * writew:
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 16-bit value to memory, using #global_qtest.
 */
void writew(uint64_t addr, uint16_t value);

/**
 * writel:
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 32-bit value to memory, using #global_qtest.
 */
void writel(uint64_t addr, uint32_t value);

/**
 * writeq:
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 64-bit value to memory, using #global_qtest.
 */
void writeq(uint64_t addr, uint64_t value);

/**
 * readb:
 * @addr: Guest address to read from.
 *
 * Reads an 8-bit value from memory, using #global_qtest.
 *
 * Returns: Value read.
 */
uint8_t readb(uint64_t addr);

/**
 * readw:
 * @addr: Guest address to read from.
 *
 * Reads a 16-bit value from memory, using #global_qtest.
 *
 * Returns: Value read.
 */
uint16_t readw(uint64_t addr);

/**
 * readl:
 * @addr: Guest address to read from.
 *
 * Reads a 32-bit value from memory, using #global_qtest.
 *
 * Returns: Value read.
 */
uint32_t readl(uint64_t addr);

/**
 * readq:
 * @addr: Guest address to read from.
 *
 * Reads a 64-bit value from memory, using #global_qtest.
 *
 * Returns: Value read.
 */
uint64_t readq(uint64_t addr);

/**
 * memread:
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer, using #global_qtest.
 */
void memread(uint64_t addr, void *data, size_t size);

/**
 * rtas_call:
 * @name: name of the command to call.
 * @nargs: Number of args.
 * @args: Guest address to read args from.
 * @nret: Number of return value.
 * @ret: Guest address to write return values to.
 *
 * Call an RTAS function, using #global_qtest
 */
uint64_t rtas_call(const char *name, uint32_t nargs, uint64_t args,
                   uint32_t nret, uint64_t ret);

/**
 * bufread:
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer and receive using a base64
 * encoding, using #global_qtest.
 */
void bufread(uint64_t addr, void *data, size_t size);

/**
 * memwrite:
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory, using #global_qtest.
 */
void memwrite(uint64_t addr, const void *data, size_t size);

/**
 * bufwrite:
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory and transmit using a base64
 * encoding, using #global_qtest.
 */
void bufwrite(uint64_t addr, const void *data, size_t size);

/**
 * qmemset:
 * @addr: Guest address to write to.
 * @patt: Byte pattern to fill the guest memory region with.
 * @size: Number of bytes to write.
 *
 * Write a pattern to guest memory, using #global_qtest.
 */
void qmemset(uint64_t addr, uint8_t patt, size_t size);

/**
 * clock_step_next:
 *
 * Advance the QEMU_CLOCK_VIRTUAL to the next deadline, using #global_qtest.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t clock_step_next(void);

/**
 * clock_step:
 * @step: Number of nanoseconds to advance the clock by.
 *
 * Advance the QEMU_CLOCK_VIRTUAL by @step nanoseconds, using #global_qtest.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t clock_step(int64_t step);

/**
 * clock_set:
 * @val: Nanoseconds value to advance the clock to.
 *
 * Advance the QEMU_CLOCK_VIRTUAL to @val nanoseconds since the VM was
 * launched, using #global_qtest.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t clock_set(int64_t val);

/**
 * big_endian:
 *
 * Returns: True if the architecture under test, via #global_qtest,
 * has a big endian configuration.
 */
bool big_endian(void);

/**
 * qtest_get_arch:
 *
 * Returns: The architecture for the QEMU executable under test.
 */
const char *qtest_get_arch(void);

/**
 * qtest_add_func:
 * @str: Test case path.
 * @fn: Test case function
 *
 * Add a GTester testcase with the given name and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
void qtest_add_func(const char *str, void (*fn)(void));

/**
 * qtest_add_data_func:
 * @str: Test case path.
 * @data: Test case data
 * @fn: Test case function
 *
 * Add a GTester testcase with the given name, data and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
void qtest_add_data_func(const char *str, const void *data,
                         void (*fn)(const void *));

/**
 * qtest_add_data_func_full:
 * @str: Test case path.
 * @data: Test case data
 * @fn: Test case function
 * @data_free_func: GDestroyNotify for data
 *
 * Add a GTester testcase with the given name, data and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 *
 * @data is passed to @data_free_func() on test completion.
 */
void qtest_add_data_func_full(const char *str, void *data,
                              void (*fn)(const void *),
                              GDestroyNotify data_free_func);

/**
 * qtest_add:
 * @testpath: Test case path
 * @Fixture: Fixture type
 * @tdata: Test case data
 * @fsetup: Test case setup function
 * @ftest: Test case function
 * @fteardown: Test case teardown function
 *
 * Add a GTester testcase with the given name, data and functions.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
#define qtest_add(testpath, Fixture, tdata, fsetup, ftest, fteardown) \
    do { \
        char *path = g_strdup_printf("/%s/%s", qtest_get_arch(), testpath); \
        g_test_add(path, Fixture, tdata, fsetup, ftest, fteardown); \
        g_free(path); \
    } while (0)

void qtest_add_abrt_handler(GHookFunc fn, const void *data);

/**
 * qmp:
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and returns the response.
 */
QDict *qmp(const char *fmt, ...);

/**
 * qmp_async:
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qmp_async(const char *fmt, ...);

/**
 * qmp_discard_response:
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and consumes the response.
 */
void qmp_discard_response(const char *fmt, ...);

/**
 * hmp:
 * @fmt...: HMP command to send to QEMU
 *
 * Send HMP command to QEMU via QMP's human-monitor-command.
 *
 * Returns: the command's output.  The caller should g_free() it.
 */
char *hmp(const char *fmt, ...);

QDict *qmp_fd_receive(int fd);
void qmp_fd_sendv(int fd, const char *fmt, va_list ap);
void qmp_fd_send(int fd, const char *fmt, ...);
QDict *qmp_fdv(int fd, const char *fmt, va_list ap);
QDict *qmp_fd(int fd, const char *fmt, ...);

/**
 * qtest_cb_for_every_machine:
 * @cb: Pointer to the callback function
 *
 *  Call a callback function for every name of all available machines.
 */
void qtest_cb_for_every_machine(void (*cb)(const char *machine));

#endif
