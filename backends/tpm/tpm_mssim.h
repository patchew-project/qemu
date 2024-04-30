/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * The code below is copied from the Microsoft/TCG Reference implementation
 *
 *  https://github.com/Microsoft/ms-tpm-20-ref.git
 *
 * In file TPMCmd/Simulator/include/TpmTcpProtocol.h
 */

#define TPM_SIGNAL_POWER_ON         1
#define TPM_SIGNAL_POWER_OFF        2
#define TPM_SIGNAL_PHYS_PRES_ON     3
#define TPM_SIGNAL_PHYS_PRES_OFF    4
#define TPM_SIGNAL_HASH_START       5
#define TPM_SIGNAL_HASH_DATA        6
/* {uint32_t BufferSize, uint8_t[BufferSize] Buffer} */
#define TPM_SIGNAL_HASH_END         7
#define TPM_SEND_COMMAND            8
/*
 * {uint8_t Locality, uint32_t InBufferSize, uint8_t[InBufferSize] InBuffer} ->
 *   {uint32_t OutBufferSize, uint8_t[OutBufferSize] OutBuffer}
 */
#define TPM_SIGNAL_CANCEL_ON        9
#define TPM_SIGNAL_CANCEL_OFF       10
#define TPM_SIGNAL_NV_ON            11
#define TPM_SIGNAL_NV_OFF           12
#define TPM_SIGNAL_KEY_CACHE_ON     13
#define TPM_SIGNAL_KEY_CACHE_OFF    14

#define TPM_REMOTE_HANDSHAKE        15
#define TPM_SET_ALTERNATIVE_RESULT  16

#define TPM_SIGNAL_RESET            17
#define TPM_SIGNAL_RESTART          18

#define TPM_SESSION_END             20
#define TPM_STOP                    21

#define TPM_GET_COMMAND_RESPONSE_SIZES  25

#define TPM_ACT_GET_SIGNALED        26

#define TPM_TEST_FAILURE_MODE       30
