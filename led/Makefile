ifneq ($(KERNELRELEASE),)
obj-m := alpha_led.o
else
KDIR ?= /home/woody/alpha/woody/linux-7.2.6/out
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
USER_CC := $(CROSS_COMPILE)gcc
HOST_CC ?= cc
CFLAGS_USER := -std=c11 -O2 -Wall -Wextra -Werror

.PHONY: all module test host-test clean
all: module test
module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
test: led_test
led_test: led_test.c
	$(USER_CC) $(CFLAGS_USER) -o $@ $<
host-test:
	$(HOST_CC) $(CFLAGS_USER) -o led_test_host led_test.c
clean:
	$(RM) led_test led_test_host
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
endif
