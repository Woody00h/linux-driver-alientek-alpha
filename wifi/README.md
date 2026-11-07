# ALPHA i.MX6ULL USB WiFi（AIC8800DC / DW）

## 当前结果与确认边界

本机 sysfs 实测 USB ID **a69c:5721**，产品字符串 **Aic MSC**，是虚拟光盘模式。
此 ID 被多个 AIC 芯片共用，**不能仅凭它确定为 DC/DW**。
本包针对切换后为 AIC8800DC/DW（包括驱动支持表中的水星 2c4e:0114、
2c4e:0126），如果切换为 D80 等其他型号，应使用对应驱动和固件。
水星 UX3 免驱版已在 ALPHA i.MX6ULL（Linux 7.2.6）实测：切换后为
**2357:0147 / AIC8800DC**，WPA2 认证、DHCP、无线网关连通及路由器主机名显示均通过。

驱动模块针对相邻 Linux `out` 构建，生成 ARM 32 位模块。
已有内核配置包含 CFG80211、MAC80211、RFKILL、USB EHCI、ChipIdea host、
USB storage、devtmpfs，无需为此 USB 网卡新增设备树节点。
请将网卡插入开发板 USB HOST 口；OTG 口需要正确的 host 模式配置。

## 构建

```sh
cd /home/woody/alpha/woody/driver/wifi
# 首次从 Git 检出时，先从固定上游版本取得固件（本仓库不提交固件二进制）。
fw_tmp=$(mktemp -d)
git clone https://github.com/Kiborgik/aic8800dc-linux-patched.git "$fw_tmp/upstream"
git -C "$fw_tmp/upstream" archive aad6a095dc795975bb16a291fc7b22025348115b fw/aic8800DC | tar -x -C vendor
./build.sh
# 可覆盖 KDIR、CROSS_COMPILE、JOBS。更换工具链后应移走 build/ 重新构建。
```

`build.sh` 不使用主机 DKMS 安装脚本，不修改主机网络，输出：

- `alpha-wifi-arm.tar.gz`：板端部署包。
- `package/lib/modules/<内核版本>/extra/`：aic_load_fw.ko、aic8800_fdrv.ko。
- `package/lib/firmware/aic8800DC/`：匹配固件及厂商功率配置。
- `package/usr/sbin/`：静态 ARM wpa_supplicant、wpa_cli、wpa_passphrase、
  usb_modeswitch，以及加载、连接、DHCP、诊断脚本。

依赖主机 make、ARM hard-float GCC、pkg-config、flex、bison 和常规 autotools
构建环境。`sources/` 保存工具的上游源码压缩包和 SHA256SUMS。
libnl 3.11 使用 `-D_GNU_SOURCE -include time.h` 兼容当前 GCC 的严格声明检查。
默认认证配置支持 **WPA2-PSK**；不包含企业 EAP 或 WPA3-SAE。

## 板端安装（root）

将部署包通过 SD 卡、U 盘或网络传到板端后：

```sh
uname -r
# 必须与 package/usr/share/alpha-wifi/kernel.release 一致。
tar -xzf alpha-wifi-arm.tar.gz -C /
# 可选：depmod -a。脚本直接 insmod，不依赖模块索引。
wifi-load.sh
```

模块必须与板端实际运行内核的配置和符号版本匹配，不能只比较版本号。
本包保留固件原目录结构，通过 aic_load_fw 的 aic_fw_path=/lib/firmware 参数
设置路径；DC/DW 固件由无线驱动在其下的 aic8800DC/ 中查找。

`wifi-load.sh` 用 usb_modeswitch 标准 eject 切换 AIC 虚拟光盘，无需 udev、
SCSI CD-ROM 驱动、libusb 动态库或桌面 NetworkManager。
网卡重新枚举后，等待最多 20 秒出现 AIC 无线接口。

## 配置和连接

准备 2.4GHz WPA2 个人网络。在板端交互输入密码，避免将密码留在命令历史中：

```sh
umask 077
printf 'SSID: '; read -r ssid
printf 'WiFi password: '; stty -echo; read -r password; stty echo; echo
{
    printf 'ctrl_interface=/run/wpa_supplicant\ncountry=CN\n'
    printf '%s\n' "$password" | wpa_passphrase "$ssid" | sed '/^[[:space:]]*#psk=/d'
} > /etc/wpa_supplicant.conf
unset password ssid
wifi-connect.sh wlan0
```

SSID 应为 1–32 字节，密码为 8–63 字符。`country=CN` 应改为实际所在地。
也可复制 `/usr/share/alpha-wifi/wpa_supplicant.conf.example` 后编辑。
若接口名称不同，按 wifi-load.sh 输出替换 wlan0。
连接脚本等候认证完成才启动 DHCP；DHCP 成功后转到后台续租，并通过 DHCP 选项 12 上报主机名 `alpha-imx6ull`。
DHCP 脚本设置接口地址、metric 600 的默认路由，并用 DHCP DNS 更新
`/etc/resolv.conf`。现有较低 metric 的以太网默认路由仍可能被优先使用。

```sh
wpa_cli -i wlan0 status           # 应为 wpa_state=COMPLETED
ip -4 addr show dev wlan0        # 应有 DHCP 地址
ip route
# 用实际 WiFi 网关地址验证无线链路：
ping -I wlan0 -c 3 192.168.1.1
```

网关 ping 成功后再验证外网和 DNS。接口出现不代表已经连接 AP。
扫描可用 `wpa_cli -i wlan0 scan`，稍等后 `wpa_cli -i wlan0 scan_results`。

如需开机连接，可在板端 `/etc/init.d/rcS` 末尾加入下面命令，先完成手动测试：

```sh
(/usr/sbin/wifi-load.sh && /usr/sbin/wifi-connect.sh wlan0) > /tmp/wifi-start.log 2>&1 &
```

## 故障诊断

```sh
wifi-diag.sh > /tmp/wifi-diag.log 2>&1
cat /tmp/wifi-diag.log
cat /tmp/wpa-wlan0.log
```

- 仍为 a69c:5721：检查 usb_modeswitch 输出、/dev/bus/usb 节点和 USB 供电。
- 切换后为 368b:8d81 等 D80：不适用本驱动，需根据真实 ID 补充对应驱动。
- 固件找不到：检查 /lib/firmware/aic8800DC、加载顺序及 dmesg 的具体文件名。
- Invalid module format / unknown symbol：换成匹配当前内核的模块；不要强制加载。
- 无接口或下载固件卡住：保存完整串口日志。上游未验证 ARMv7 单核平台，
  编译通过不等于实板运行通过。
- 认证超时：检查 SSID、密码、AP 是否提供 2.4GHz WPA2-PSK，以及法规域。

## 来源与许可证

驱动：https://github.com/Kiborgik/aic8800dc-linux-patched
固定版本：aad6a095dc795975bb16a291fc7b22025348115b（基于 AIC 6.4.3.0）。
驱动源码及许可证保留在 vendor/；固件二进制请按构建步骤从固定上游版本获取。
工具来源：w1.fi/releases/wpa_supplicant-2.11.tar.gz、
github.com/thom311/libnl/releases/tag/libnl3_11_0、
github.com/libusb/libusb/releases/tag/v1.0.29、
www.draisberghof.de/usb_modeswitch/（2.6.1）。
源代码保存在 vendor/ 和 sources/，二进制包中包含许可证副本。
再分发时应同时提供对应源码、构建脚本及许可证，并确认固件的再分发条件。
