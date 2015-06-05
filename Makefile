
#License:

#    This code is licenced under the GPL.

# Makefile for 2.6 based kernels
#
KERNEL_BUILD	:= /lib/modules/`uname -r`/build
DRV_DIR		:= `uname -r`

obj-m		:= smi.o
modules modules_install clean:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) $@

