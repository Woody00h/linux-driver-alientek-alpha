KERNEL_SRC ?= /home/woody/alpha/woody/linux-7.2.6
KDIR ?= $(KERNEL_SRC)/out
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror
DTB := imx6ull-alientek-alpha-v27-emmc.dtb

.PHONY: all dtb kernel config clean
all: lcd_test dtb
lcd_test: lcd_test.c
	$(CC) $(CFLAGS) -o $@ $<
# Explicit sync keeps the driver directory the authoritative copy.
dtb:
	cmp -s alpha-lcd.dtsi $(KERNEL_SRC)/arch/arm/boot/dts/nxp/imx/alpha-lcd.dtsi || cp alpha-lcd.dtsi $(KERNEL_SRC)/arch/arm/boot/dts/nxp/imx/alpha-lcd.dtsi
	$(MAKE) -C $(KERNEL_SRC) O=$(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) nxp/imx/$(DTB)
	cp $(KDIR)/arch/arm/boot/dts/nxp/imx/$(DTB) .
config:
	cd $(KERNEL_SRC) && scripts/kconfig/merge_config.sh -m -O $(KDIR) $(KDIR)/.config $(CURDIR)/lcd.config
	$(MAKE) -C $(KERNEL_SRC) O=$(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) olddefconfig
kernel:
	$(MAKE) -C $(KERNEL_SRC) O=$(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) zImage
clean:
	$(RM) lcd_test $(DTB)
