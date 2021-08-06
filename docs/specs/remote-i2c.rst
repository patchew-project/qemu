=================
Remote I2C Device
=================

The remote I2C device is connected directly to the I2C controller inside QEMU,
and the external I2C device is outside of QEMU. The communication between the
external and remote I2C devices is done through the character device provided
by QEMU and follows the remote I2C protocol.

Remote I2C Protocol
===================
The remote I2C device implements three functions of the struct I2CSlaveClass:

* event
* recv
* send

Exactly one byte is written or read from the character device at a time,
so these functions may read/write to the character device multiple times.
Each byte may be a command or a data byte. The command are outlined
in enum RemoteI2CCommand. The protocol describes the expected behavior
of the external I2C device in response to the the commands.

event
=====
A subset of the RemoteI2CCommand corresponds exactly to the enum i2c_event.
They are:

* REMOTE_I2C_START_RECV
* REMOTE_I2C_START_SEND
* REMOTE_I2C_FINISH
* REMOTE_I2C_NACK

The event function of remote I2C writes the command to the external I2C device.
The external device should call its event function to process the command as
an event and write back the return value to remote I2C. This value is then
returned by the event function of remote I2C.

recv
====
The recv function of remote I2C writes the RemoteI2CCommand REMOTE_I2C_RECV to
the external I2C device. The external device should call its recv function
and write back the return value to remote I2C. This value is then returned by
the recv function of remote I2C.

send
====
The send function of remote I2C writes the RemoteI2CCommand REMOTE_I2C_SEND
followed by the data to the external I2C device. The external device should
call its send function to process the data and write the return value back to
remote I2C. This value is then returned by the send function of remote I2C.
