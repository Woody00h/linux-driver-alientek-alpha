ifneq ($(KERNELRELEASE),)
obj-m := alpha_audio.o
else
KDIR ?= /home/woody/alpha/woody/linux-7.2.6/out
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
USER_CC ?= $(CROSS_COMPILE)gcc
HOST_CC ?= cc
USER_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror
.PHONY: all module test host-test clean
all: module test
module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
test: audio_test audio_ctl
audio_test: audio_test.c
	$(USER_CC) $(USER_CFLAGS) -o $@ $< -lm
audio_ctl: audio_ctl.c
	$(USER_CC) $(USER_CFLAGS) -o $@ $<
host-test:
	$(HOST_CC) $(USER_CFLAGS) -o audio_test_host audio_test.c -lm
	$(HOST_CC) $(USER_CFLAGS) -o audio_ctl_host audio_ctl.c
clean:
	$(RM) audio_test audio_test_host audio_ctl audio_ctl_host
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
endif
