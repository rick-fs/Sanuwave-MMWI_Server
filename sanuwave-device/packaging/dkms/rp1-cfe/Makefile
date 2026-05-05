# Out-of-tree build for the SANUWAVE-patched rp1-cfe module.
# Mirrors drivers/media/platform/raspberrypi/rp1_cfe/Makefile from the
# upstream kernel tree.

obj-m := rp1-cfe.o
rp1-cfe-objs := cfe.o csi2.o pisp_fe.o dphy.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
