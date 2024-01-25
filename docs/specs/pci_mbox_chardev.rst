Remote PCIe Protocol
====================

Design
------
The communication or this device is done via a chardev. It is bidirectional:
QEMU can send requests to devices and the device can send MSI/DMA requests
to QEMU. All registers are encoded in Little Endian.

To distinguish between the two types of messages, any message with an error
code described below is a response, otherwise it is a request. The remote
PCIe device is responsible for guaranteeing the messages sent out are
integrated.

The highest bit for the first byte reflects whether a message is a request
or response - 0 for request and 1 for response.

For responses, the rest of the bits reflect the error code.
For requests, the rest of the bit is the command code specified below.


Initialization
--------------
During initialization of the remote PCIe device in QEMU, it needs to specify
a few configuration parameters. The PCIe connector is responsible for
getting these configuration parameters and passing them in as QDev
properties
The fields include:
 1. PCI endpoint device identifiers (google3/platforms/asic_sw/proto/device_identifiers.proto).
     a. Vendor ID
     b. Device ID
     c. Subsystem Vendor ID
     d. Subsystem Device ID
     e. Class Code
     f. Subclass
     g. Programming Interface
     h. Revision ID
 2. Number of BARs and the size of each BAR
 3. Whether DMA is supported.
 4. Number of MSI vectors supported (must be power of 2, up to 32)

Request and Reponse Breakdowns
------------------------------
PCI Endpoint R/W Request
~~~~~~~~~~~~~~~~~~~~~~~~
QEMU can send this request to endpoint.
ReadData
Request:
+------+------+--------+-----------+-----------+
| Byte | 0    | 0x1    | 0x2 ~ 0x9 | 0xa       |
| Data | 0x01 | bar_no | offset    | read_size |
+------+------+--------+-----------+-----------+
(read_size in number of bytes, must be between 1 and 8)
Response:
Success:
+------+------+-------------------+
| Byte | 0    | 0x1 ~ read_size+1 |
| Data | 0x80 | data              |
+------+------+-------------------+
Failure:
+------+-------------------+
| Byte | 0                 |
| Data | 0x80 | error_code |
+------+-------------------+

WriteData
Request:
+------+------+--------+-----------+------------+------+
| Byte | 0    | 0x1    | 0x2 ~ 0x9 | 0xa        | 0xb~ |
| Data | 0x02 | bar_no | offset    | write_size | data |
+------+------+--------+-----------+------------+------+
(write_size in number of bytes, must be between 1 and 8)
Response:
+------+-------------------+
| Byte | 0                 |
| Data | 0x80 | error_code |
+------+-------------------+

PCIe DMA Request
~~~~~~~~~~~~~~~~
The endpoint can send this request to QEMU.
ReadData
Request:
+------+------+-----------+------------+
| Byte | 0    | 0x1 ~ 0x8 | 0x9 ~ 0x10 |
| Data | 0x03 | address   | read_size  |
+------+------+-----------+------------+
Response:
Success:
+------+------+-------------------+
| Byte | 0    | 0x1 ~ read_size+1 |
| Data | 0x80 | data              |
+------+------+-------------------+
Failure:
+------+-------------------+
| Byte | 0                 |
| Data | 0x80 | error_code |
+------+-------------------+

WriteData
Request:
+------+------+-----------+-------------+-------+
| Byte | 0    | 0x1 ~ 0x8 | 0x9 ~ 0x10  | 0x11~ |
| Data | 0x04 | address   | write_size  | data  |
+------+------+-----------+-------------+-------+
Response:
+------+-------------------+
| Byte | 0                 |
| Data | 0x80 | error_code |
+------+-------------------+

PCIe MSI/MSIx request
~~~~~~~~~~~~~~~~~~~~~
The endpoint can send this request to QEMU.
Request:
+------+------+-----------+
| Byte | 0    | 0x1 ~ 0x4 |
| Data | 0x05 | VectorNo  |
+------+------+-----------+
Response:
+------+-------------------+
| Byte | 0                 |
| Data | 0x80 | error_code |
+------+-------------------+

PCIe Config Space Read/Write
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
QEMU can send this request to endpoint.
ReadConfigData
Request:
+------+------+-----------+------------+
| Byte | 0    | 0x1 ~ 0x8 | 0x9        |
| Data | 0x06 | address   | read_size  |
+------+------+-----------+------------+
(read_size in number of bytes, must be between 1 and 8)
Response:
Success:
+------+------+-------------------+
| Byte | 0    | 0x1 ~ read_size+1 |
| Data | 0x80 | data              |
+------+------+-------------------+
Failure:
+------+-------------------+
| Byte | 0                 |
| Data | 0x80 | error_code |
+------+-------------------+

WriteConfigData
Request:
+------+------+-----------+------------+-------+
| Byte | 0    | 0x1 ~ 0x8 | 0x9        | 0xa~ |
| Data | 0x07 | address   | write_size | data  |
+------+------+-----------+------------+-------+
Response:
+------+-------------------+
| Byte | 0                 |
| Data | 0x80 | error_code |
+------+-------------------+
 */
