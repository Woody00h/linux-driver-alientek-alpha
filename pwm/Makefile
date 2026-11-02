ifneq ($(KERNELRELEASE),)
obj-m := alpha_pwm.o
else
KDIR ?= /home/woody/alpha/woody/linux-7.2.6/out
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
.PHONY: all clean
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
endif
