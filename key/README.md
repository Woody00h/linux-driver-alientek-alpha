# ALIENTEK ALPHA i.MX6ULL 按键驱动

ALPHA V2.4 板载 KEY0 经 UART1_CTS（J2 第 48 脚、JP6 第 18 脚）连接
CPU GPIO1_IO18，空闲高电平，按下低电平。底板 R15 为 10K 上拉。
驱动使用 platform driver、GPIO descriptor 和
Linux input 子系统，按键编码为 KEY_ENTER（28）。接口是 `/dev/input/eventX`。

## 文件与编译

- `alpha_key.c`：双边沿中断、20ms 软件消抖的内核模块源码。
- `alpha-key.dtsi`：引脚复用、内部 100K 上拉和低电平有效配置。
- `key_test.c`：读取 input 事件，打印 PRESSED / RELEASED。
- `alpha_key.ko`、`key_test`：编译生成的 ARM 模块和测试程序。
- `imx6ull-alientek-alpha-v27-emmc.dtb`：编译生成的设备树，包含按键和原有 LED。

编译产物由 .gitignore 排除，不纳入源码提交。

```sh
cd /home/woody/alpha/woody/driver/key
make
# 指定其他开发板内核构建目录/交叉工具链：
make KDIR=/path/to/kernel-build CROSS_COMPILE=arm-linux-gnueabihf-
```

默认构建目录为相邻内核源码的 `out`。必须与板端运行内核的配置、版本和
Module.symvers 匹配。内核需要 CONFIG_INPUT、CONFIG_INPUT_EVDEV、CONFIG_OF、
CONFIG_GPIOLIB、CONFIG_MODULES 及 i.MX GPIO/pinctrl 支持。测试程序使用 armhf
动态链接，需要板端匹配的 libc 和加载器；可通过工具链或 USER_CC 覆盖。

## 设备树与部署

已将片段复制到当前内核 `arch/arm/boot/dts/nxp/imx/alpha-key.dtsi`，并在
`imx6ull-alientek-alpha-v27-emmc.dts` 末尾加入：

```dts
#include "alpha-key.dtsi"
```

其中 `key-gpios = <&gpio1 18 GPIO_ACTIVE_LOW>` 对应 GPIO1_IO18；引脚配置
`0x1b0b0` 启用迟滞和上拉。确保此引脚没有其他 GPIO 消费者或外设占用。
移植到其他板级 DTS 时，将片段放入其目录，在 imx6ull.dtsi 之后、文件末尾 include。

在内核源码目录重新编译：

```sh
make O=out ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
    nxp/imx/imx6ull-alientek-alpha-v27-emmc.dtb
```

将新 DTB 部署到 U-Boot 实际加载的位置并重启。仅加载模块但没有使用新 DTB，
不会出现按键设备。将 alpha_key.ko 和 key_test 复制到开发板后，以 root 执行：

```sh
insmod ./alpha_key.ko
cat /proc/bus/input/devices
```

找到 `Name="alpha-key"` 对应的 `Handlers=... eventX`，用该编号运行：

```sh
./key_test /dev/input/eventX
# 按下显示 PRESSED，松开显示 RELEASED；Ctrl+C 退出
rmmod alpha_key
```

`eventX` 是示意，需替换为实际编号，例如 `/dev/input/event0`。每次稳定按下/
松开应各打印一次。程序检查设备名以避免误读其他输入设备，事件溢出则提示重启。
测试开始前已按住的状态不会作为历史事件重放；松开再按即可观察完整一对事件。

## 消抖与生命周期

1. 上升沿和下降沿都触发 IRQ，中断中只更新截止时间并安排 delayed_work。
2. 每次新边沿都将确认时间推迟到该边沿之后 20ms，因此抖动会重新计时。
3. 工作回调再次检查实际单调时钟，确保至少经过 20ms，然后读取 GPIO 逻辑值。
   GPIO_ACTIVE_LOW 将物理低电平转换为逻辑 1，表示按下。
4. 仅在稳定状态变化时上报 EV_KEY 和 SYN_REPORT，不启用长按自动重复。
5. IRQ 和采样通过自旋锁同步；卸载先 free_irq，再 cancel_delayed_work_sync，
   最后由 devm 释放 input 设备和 GPIO。

这是中断触发的延迟采样，不做周期轮询。20ms 是最短稳定等待时间，受 HZ=100
时钟粒度和工作队列调度影响，上报可能略晚。持续不足 20ms 的短脉冲会被过滤。
驱动针对片上非睡眠 GPIO，若改用需要睡眠的 GPIO 扩展器，probe 会拒绝。
未实现系统挂起唤醒功能。

## 验证与排查

本机使用现有 Linux 7.2.6/out 完整编译模块（W=1、MODPOST），ARM 和主机
测试程序在 -Wall -Wextra -Werror 下通过，设备树编译通过，驱动 checkpatch
检查为 0 errors / 0 warnings。用户已在 ALPHA V2.4 实板验证：加载模块后
出现 alpha-key / event0，连续六次按下和松开均成对输出 PRESSED / RELEASED。
20ms 消抖时长尚未通过波形测量验证，卸载和系统电源管理尚未单独实板测试。

没有 event 节点时检查新 DTB 是否加载、`/proc/device-tree/alpha-key` 是否存在、
`dmesg` 的 probe 日志及 evdev 配置。GPIO busy 时检查重复占用；模块版本错误时
核对板端 `uname -r` 与模块 vermagic。测试无法执行时核对 ARM ABI/libc。

## 按下无事件时诊断

驱动提供 `/sys/bus/platform/devices/alpha-key/raw_level`，读取物理电平，
未按下应为 1，持续按住应为 0。此接口仅用于手动诊断，不改变中断检测方式。

```sh
cat /sys/bus/platform/devices/alpha-key/raw_level
# 持续按住按键时再次读取，应变为 0
cat /sys/bus/platform/devices/alpha-key/raw_level
grep alpha-key /proc/interrupts
# 按下、松开数次后再次读取，中断计数应增加
grep alpha-key /proc/interrupts
```

电平不变时优先检查实际按键引脚、连接和运行时 pinmux；电平变化而中断计数
不增加时检查 GPIO 中断配置；两者正常而无事件时检查工作回调和事件读取。
工作队列已改用 Linux 7.2 的 system_percpu_wq，消除旧 system_wq 的弃用提示。
