# Bootloaders for PacSat

## About
This repository contains bootloader software for the PacSat project.
The code is set up as a TI CCS (Eclipse) project.  You should be
able to clone it into your own copy of CCS.

This code has two output images (set as build configurations):

RAMLoader - Takes an image from the serial port and stores it in RAM.

FLASHLoader - This image is loaded into RAM.  It takes an image from
the serial port and loads it in FLASH.

## RAMLoader

This code is loaded into the lower 64K of RAM on the PacSat board and
comes up first.  The device will check a line to see if it should
enter the loader or jump to the main payload at address 0x10000 in
memory, which should be the normal software on the board.

If the line is not asserted, it starts a bootloader as described in
https://www.ti.com/lit/ug/slau887a/slau887a.pdf?ts=1777909805211 on
the serial port.  This bootloader is only capable of loading data into
RAM, it does not have the ability to write to FLASH.

The idea is that you load a second-stage bootloader into RAM, the
FLASHLoader, that is capable of loading the software into FLASH.  This
is necessary because you cannot be executing from FLASH while writing
to FLASH.

The loader can currently use one of two lines to do control it: ECLK
or the RX line of the serial port.  This are defined in
loader_config.h to set this.  RX line is the default.

### RX line

If using the RX line, it must be pulled high and kept high for 5
seconds to invoke the loader.  This way, if and data is transmitted on
the serial port, it will pull the RX line low and boot the payload
software.

### ECLK line

If using ECLK, it must be pulled low to enable to loader.  There is no
delay.

## FLASHLoader

This loader runs in RAM and load an image into FLASH.  It uses the
same basic protocol and code as the RAMLoader, except it doesn't do
processor initialization (the RAMLoader has already done that) or look
at the boot control line line.  And of course it stores the image in
FLASH.

## Build Setup

This uses the same build setup as PacSatSw, see that for details.
