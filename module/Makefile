ifneq ($(KERNELRELEASE),)
obj-m := iclcam.o

else
KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

.PHONY: clean

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
endif
