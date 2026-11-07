#!/bin/sh
set -eu
[ "$(id -u)" = 0 ] || { echo '请以 root 运行' >&2; exit 1; }
KREL=$(cat /usr/share/alpha-wifi/kernel.release)
[ "$(uname -r)" = "$KREL" ] || { echo "模块要求内核 $KREL，当前为 $(uname -r)" >&2; exit 1; }
# usbfs 设备节点由 devtmpfs 提供，不依赖 udev 或 SCSI CD-ROM 支持。
for dev in /sys/bus/usb/devices/*; do
    [ -f "$dev/idVendor" ] || continue
    [ "$(cat "$dev/idVendor")" = a69c ] || continue
    pid=$(cat "$dev/idProduct")
    case "$pid" in
        5721|5722|5723|5724|5725|5726|5727|572a|572c|572f)
            /usr/sbin/usb_modeswitch -K -v 0xa69c -p "0x$pid" \
                -b "$(cat "$dev/busnum")" -g "$(cat "$dev/devnum")" || {
                echo 'USB 模式切换失败，请检查 dmesg 和 USB 供电。' >&2; exit 1;
            }
            ;;
    esac
done
[ -d /sys/module/aic_load_fw ] || insmod "/lib/modules/$KREL/extra/aic_load_fw.ko" aic_fw_path=/lib/firmware
[ -d /sys/module/aic8800_fdrv ] || insmod "/lib/modules/$KREL/extra/aic8800_fdrv.ko"
i=0
while [ "$i" -lt 20 ]; do
    for net in /sys/class/net/*; do
        [ -d "$net/phy80211" ] || [ -d "$net/wireless" ] || continue
        # This driver attaches the netdev to the USB device; the driver
        # binding is on its child USB interface (for example 1-1:1.0).
        for link in "$net/device/driver" "$net/device/"*:*/driver; do
            driver=$(readlink "$link" 2>/dev/null) || continue
            [ "${driver##*/}" = aic8800_fdrv ] || continue
            echo "WiFi 就绪: ${net##*/}"
            exit 0
        done
    done
    sleep 1
    i=$((i + 1))
done
echo '未出现 AIC 无线接口。运行 wifi-diag.sh；若切换后为 D80/其他芯片，本包不适用。' >&2
exit 1
