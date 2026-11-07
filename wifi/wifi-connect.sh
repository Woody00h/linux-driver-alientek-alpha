#!/bin/sh
set -eu
IFACE=${1:-wlan0}
CONF=${2:-/etc/wpa_supplicant.conf}
[ "$(id -u)" = 0 ] || { echo '请以 root 运行' >&2; exit 1; }
case "$IFACE" in ''|*[!a-zA-Z0-9_.-]*) echo '无效接口名' >&2; exit 1;; esac
[ -f "$CONF" ] || { echo "找不到配置: $CONF" >&2; exit 1; }
is_wireless() {
    [ -d "/sys/class/net/$1/phy80211" ] || [ -d "/sys/class/net/$1/wireless" ]
}
is_wireless "$IFACE" || /usr/sbin/wifi-load.sh
is_wireless "$IFACE" || { echo "没有无线接口 $IFACE" >&2; exit 1; }
mkdir -p /run/wpa_supplicant
chmod 600 "$CONF"
ip link set "$IFACE" up
LOG="/tmp/wpa-$IFACE.log"
if ! /usr/sbin/wpa_cli -p /run/wpa_supplicant -i "$IFACE" ping 2>/dev/null | grep -q PONG; then
    if ! /usr/sbin/wpa_supplicant -B -D nl80211 -i "$IFACE" -c "$CONF" \
        -P "/run/wpa_supplicant-$IFACE.pid" > "$LOG" 2>&1; then
        echo "wpa_supplicant 启动失败，查看 $LOG" >&2
        exit 1
    fi
    if [ ! -S "/run/wpa_supplicant/$IFACE" ]; then
        echo "未创建 WiFi 控制接口，请检查 $CONF 中的 ctrl_interface=/run/wpa_supplicant，并查看 $LOG" >&2
        exit 1
    fi
else
    /usr/sbin/wpa_cli -p /run/wpa_supplicant -i "$IFACE" reconfigure | grep -q OK
fi
i=0
while [ "$i" -lt 40 ]; do
    if /usr/sbin/wpa_cli -p /run/wpa_supplicant -i "$IFACE" status 2>/dev/null | grep -q '^wpa_state=COMPLETED$'; then
        udhcpc -i "$IFACE" -p "/run/udhcpc-$IFACE.pid" -s /usr/sbin/wifi-dhcp.sh -n -t 5 -x hostname:alpha-imx6ull
        ip -4 addr show dev "$IFACE"
        exit 0
    fi
    sleep 1
    i=$((i + 1))
done
echo "WiFi 认证超时，请检查 SSID、密码及 2.4GHz 频段；可用 wpa_cli -i $IFACE status 查看状态。" >&2
exit 1
