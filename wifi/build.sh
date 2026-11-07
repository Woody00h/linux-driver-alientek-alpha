#!/bin/sh
set -eu
cd "$(dirname "$0")"
BASE=$PWD
KDIR=${KDIR:-$BASE/../../linux-7.2.6/out}
CROSS_COMPILE=${CROSS_COMPILE:-arm-linux-gnueabihf-}
JOBS=${JOBS:-8}
PREFIX=$BASE/build/sysroot
STAGE=$BASE/package
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig
export PKG_CONFIG_PATH=
mkdir -p "$PREFIX" "$STAGE/usr/sbin" "$STAGE/usr/share/alpha-wifi" "$STAGE/lib/firmware"
for archive in sources/*.tar.*; do
    name=$(basename "$archive" | sed 's/\.tar\..*//')
    test -d "build/$name" || tar -xf "$archive" -C build
done
# Use the release embedded in the built kernel, even if Git metadata changed later.
KREL=$(LC_ALL=C strings "$KDIR/vmlinux" | awk '/^Linux version / && !seen++ {print $3}')
case "$KREL" in ''|*[!a-zA-Z0-9._+-]*) echo "Invalid kernel release: $KREL" >&2; exit 1;; esac
make -C "$KDIR" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" KERNELRELEASE="$KREL" include/generated/utsrelease.h
make -C "$KDIR" M="$BASE/vendor/drivers/aic8800" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" KERNELRELEASE="$KREL" -j"$JOBS" modules
MODDIR=$STAGE/lib/modules/$KREL/extra
mkdir -p "$MODDIR"
cp vendor/drivers/aic8800/aic_load_fw/aic_load_fw.ko "$MODDIR/"
cp vendor/drivers/aic8800/aic8800_fdrv/aic8800_fdrv.ko "$MODDIR/"
cp -R vendor/fw/aic8800DC "$STAGE/lib/firmware/"
(
    cd build/libnl-3.11.0
    test -f Makefile || ./configure --host="${CROSS_COMPILE%-}" --prefix="$PREFIX" --disable-shared --enable-static --disable-cli --disable-pthreads
    make -j"$JOBS" CFLAGS="-O2 -D_GNU_SOURCE -include time.h"
    make install
)
(
    cd build/libusb-1.0.29
    test -f Makefile || ./configure --host="${CROSS_COMPILE%-}" --prefix="$PREFIX" --disable-shared --enable-static --disable-udev
    make -j"$JOBS"
    make install
)
(
    cd build/usb-modeswitch-2.6.1
    make usb_modeswitch CC="${CROSS_COMPILE}gcc" CFLAGS="-O2 -std=gnu11" LDFLAGS=-static LIBS="$(pkg-config --static --cflags --libs libusb-1.0)"
    cp usb_modeswitch "$STAGE/usr/sbin/"
)
(
    cd build/wpa_supplicant-2.11/wpa_supplicant
    cp "$BASE/wpa.config" .config
    make -j"$JOBS" CC="${CROSS_COMPILE}gcc -std=gnu11" LDFLAGS="-static -L$PREFIX/lib" wpa_supplicant wpa_cli wpa_passphrase
    cp wpa_supplicant wpa_cli wpa_passphrase "$STAGE/usr/sbin/"
)
cp wifi-load.sh wifi-connect.sh wifi-dhcp.sh wifi-diag.sh "$STAGE/usr/sbin/"
cp wpa_supplicant.conf.example "$STAGE/usr/share/alpha-wifi/"
printf '%s\n' "$KREL" > "$STAGE/usr/share/alpha-wifi/kernel.release"
"${CROSS_COMPILE}strip" "$STAGE"/usr/sbin/wpa_* "$STAGE/usr/sbin/usb_modeswitch"
sha256sum sources/*.tar.* > sources/SHA256SUMS
cp vendor/LICENSE "$STAGE/usr/share/alpha-wifi/driver-LICENSE"
cp build/wpa_supplicant-2.11/COPYING "$STAGE/usr/share/alpha-wifi/wpa-COPYING"
cp build/libnl-3.11.0/COPYING "$STAGE/usr/share/alpha-wifi/libnl-COPYING"
cp build/libusb-1.0.29/COPYING "$STAGE/usr/share/alpha-wifi/libusb-COPYING"
cp build/usb-modeswitch-2.6.1/COPYING "$STAGE/usr/share/alpha-wifi/usb-modeswitch-COPYING"
tar -czf alpha-wifi-arm.tar.gz -C "$STAGE" ./lib/modules/"$KREL" ./lib/firmware ./usr
echo "Ready: $BASE/alpha-wifi-arm.tar.gz"
