#!/bin/sh
uname -a
for dev in /sys/bus/usb/devices/*; do
    [ -f "$dev/idVendor" ] || continue
    printf '%s %s:%s ' "${dev##*/}" "$(cat "$dev/idVendor")" "$(cat "$dev/idProduct")"
    cat "$dev/product" 2>/dev/null || echo
 done
lsmod
ip addr
ip route
for net in /sys/class/net/*; do
    [ -d "$net/phy80211" ] || [ -d "$net/wireless" ] || continue
    [ -S "/run/wpa_supplicant/${net##*/}" ] || continue
    /usr/sbin/wpa_cli -p /run/wpa_supplicant -i "${net##*/}" status
 done
dmesg | tail -120
