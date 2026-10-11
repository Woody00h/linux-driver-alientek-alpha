ifneq ($(KERNELRELEASE),)
obj-m := alpha_key.o
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
test: key_test
key_test: key_test.c
	$(USER_CC) $(CFLAGS_USER) -o $@ $<
host-test:
	$(HOST_CC) $(CFLAGS_USER) -o key_test_host key_test.c
clean:
	$(RM) key_test key_test_host
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
endif
