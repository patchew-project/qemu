CPACF Support
=============

CPACF
-----

CP Assist for Cryptographic Function (CPACF) is a hardware-integrated
coprocessor feature built into every  processor core of IBM Z and
LinuxONE mainframes (s390x architecture). It provides high-speed,
hardware-accelerated encryption and hashing directly on the CPU.

CPACF provides a set of z/Architecture instructions (known as Message
Security Assist or MSA) that execute cryptographic operations
synchronously with the main processor.

- Symmetric Encryption: Support for AES (128, 192, 256-bit), DES, and
  Triple-DES (TDES).
- Hashing: Acceleration for SHA-1, SHA-2 (up to SHA-512), SHA-3 and
  SHAKE.
- Random Number Generation: Pseudo Random Number Generator (PRNG) and
  a hardware-based True Random Number Generator (TRNG).
- Asymmetric Support: Elliptic Curve Cryptography (ECC) primitives
  P-256, P-384, P-521, Montgomery/Edwards curves (e.g., Ed25519).

CPACF instructions
------------------

Here is a list of implemented CPACF instructions and the supported
functions for each instruction:

KDSA (COMPUTE DIGITAL SIGNATURE AUTHENTICATION)
- Function code 0x00 - Function Query

KIMD (COMPUTE INTERMEDIATE MESSAGE DIGEST)
- Function code 0x00 - Function Query
- Function code 0x02 - CPACF_KIMD_SHA_256
- Function code 0x03 - CPACF_KIMD_SHA_512

KLMD (COMPUTE LAST MESSAGE DIGEST)
- Function code 0x00 - Function Query
- Function code 0x02 - CPACF_KLMD_SHA_256
- Function code 0x03 - CPACF_KLMD_SHA_512

KM (CIPHER MESSAGE)
- Function code 0x00 - Function Query
- Function code 0x12 - CPACF_KM_AES_128
- Function code 0x13 - CPACF_KM_AES_192
- Function code 0x14 - CPACF_KM_AES_256
- Function code 0x1a - CPACF_KM_PAES_128
- Function code 0x1b - CPACF_KM_PAES_192
- Function code 0x1c - CPACF_KM_PAES_256
- Function code 0x32 - CPACF_KM_XTS_128
- Function code 0x34 - CPACF_KM_XTS_256
- Function code 0x3a - CPACF_KM_PXTS_128
- Function code 0x3c - CPACF_KM_PXTS_256

KMAC (COMPUTE MESSAGE AUTHENTICATION CODE)
- Function code 0x00 - Function Query

KMC (CIPHER MESSAGE WITH CHAINING)
- Function code 0x00 - Function Query
- Function code 0x12 - CPACF_KMC_AES_128
- Function code 0x13 - CPACF_KMC_AES_192
- Function code 0x14 - CPACF_KMC_AES_256
- Function code 0x1a - CPACF_KMC_PAES_128
- Function code 0x1b - CPACF_KMC_PAES_192
- Function code 0x1c - CPACF_KMC_PAES_256

KMCTR (CIPHER MESSAGE WITH COUNTER)
- Function code 0x00 - Function Query
- Function code 0x12 - CPACF_KMCTR_AES_128
- Function code 0x13 - CPACF_KMCTR_AES_192
- Function code 0x14 - CPACF_KMCTR_AES_256
- Function code 0x1a - CPACF_KMCTR_PAES_128
- Function code 0x1b - CPACF_KMCTR_PAES_192
- Function code 0x1c - CPACF_KMCTR_PAES_256

KMF (CIPHER MESSAGE WITH CIPHER FEEDBACK)
- not supported

KMO (CIPHER MESSAGE WITH OUTPUT FEEDBACK)
- not supported

PCC (PERFORM CRYPTOGRAPHIC COMPUTATION)
- Function code 0x00 - Function Query
- Function code 0x32 - compute XTS param AES-128
- Function code 0x34 - compute XTS param AES-256
- Function code 0x3a - compute XTS param Encrypted AES-128
- Function code 0x3c - compute XTS param Encrypted AES-256

PCKMO (PERFORM CRYPTOGRAPHIC KEY MANAGEMENT OPERATION)
- Function code 0x00 - Function Query
- Function code 0x12 - CPACF_PCKMO_ENC_AES_128_KEY
- Function code 0x13 - CPACF_PCKMO_ENC_AES_192_KEY
- Function code 0x14 - CPACF_PCKMO_ENC_AES_256_KEY

PRNO (PERFORM RANDOM NUMBER OPERATION)
- Function code 0x00 - Function Query
- Function code 0x72 - CPACF_PRNO_TRNG

Note that the use of a not supported CPACF instruction (KMF and KMO)
or invocation of a not listed function will result in a Specification
Exception.

Not listed CPACF instructions (KMF, KMO) cause an Operation Exception
when used. Not listed functions cause a Specification Exception when
called. If only the query function is listed (KDSA), then the query
function will return a function status word with all but the query
function bit set to 0.
