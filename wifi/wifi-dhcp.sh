#!/bin/sh
# udhcpc 回调；客户端取得租约后转入后台并持续续租。
set -eu
case "${1:-}" in
    deconfig) ip -4 addr flush dev "$interface" ;;
    bound|renew)
        # BusyBox ifconfig 接受 DHCP 返回的点分十进制掩码。
        ifconfig "$interface" "$ip" netmask "${subnet:-255.255.255.0}" up
        if [ -n "${router:-}" ]; then
            set -- $router
            ip route replace default via "$1" dev "$interface" metric 600
        fi
        if [ -n "${dns:-}" ]; then
            : > /etc/resolv.conf
            for server in $dns; do printf 'nameserver %s\n' "$server" >> /etc/resolv.conf; done
        fi
        ;;
esac
