Linux Kernel Driver for Intel F200/SR300

As of this writing it is a work in progress. 
The UVC driver already handles video. 
This driver is for other features such as factory calibration.

The module directory includes the kernel module. Make it, and insmod.

The test directory has a userspace program to communicate with the module. 
Currently only supports reading temperature.

The calibration directory has a libusb program that retrieves and prints factory calibration.
./prop -c

fix-permissions is a script to add the video group to the device node so that it can be used as 
a normal user.

