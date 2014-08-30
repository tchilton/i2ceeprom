i2ceeprom
=========

Linux I2C EEPROM manipulation - read, write, fill, verify and test I2C EEPROM devices

- Configurable I2C bus and device addresses
- Configurable device page size and device size

- Read device contents to a file
- Write device contents from a file
- Verify device to a file
- Dump contents of device in hexdump format

- Test operations comprising of a fill to one of several standard test patterns
  (zero's, one's, checkerboard, inverse checkerboard, incremental +3)

- Read after write verification of all fill operations

- Full documentation in standard manpage format


Installation instructions

Download the source
Review the code if you wish
make
make install
man i2ceeprom

