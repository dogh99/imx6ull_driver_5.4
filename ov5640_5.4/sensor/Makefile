# Build against a configured and prepared NXP linux-imx 5.4 tree.
ifneq ($(KERNELRELEASE),)
obj-m := myov5640.o
else
KERNELDIR ?=
ARCH := arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

.PHONY: all modules clean check-kernel
all: modules

check-kernel:
	@test -n "$(KERNELDIR)" || { echo "Set KERNELDIR to your prepared NXP Linux 5.4 kernel tree"; exit 1; }
	@test -f "$(KERNELDIR)/Makefile" || { echo "KERNELDIR does not contain a kernel Makefile"; exit 1; }

modules: check-kernel
	$(MAKE) -C "$(KERNELDIR)" M="$(CURDIR)" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean: check-kernel
	$(MAKE) -C "$(KERNELDIR)" M="$(CURDIR)" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
endif
