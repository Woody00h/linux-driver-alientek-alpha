# JP6 PIN20 硬件 PWM（ALIENTEK ALPHA i.MX6ULL / Linux 7.2.6）

输出路径：PWM3 硬件控制器 → GPIO1_IO04 的 PWM3_OUT 复用 → JP6 第 20 脚。
JP6 第 32 脚是 GND。输出为 3.3V 逻辑信号。

波形由 SoC PWM 外设持续产生，不使用 GPIO 翻转、hrtimer 或用户态循环。
底层寄存器和时钟由内核 drivers/pwm/pwm-imx27.c 驱动；本目录 alpha_pwm.c
是配套的 PWM consumer 平台驱动，提供频率、占空比、使能的统一控制接口。
原 alpha_beeper.c、pwm-gpio.c 及蜂鸣器设备树已移除，不再控制板载 BEEP。

## 文件和编译

- alpha_pwm.c：控制驱动，默认关闭，卸载和关机时关闭硬件 PWM。
- alpha-pwm.dtsi：使能 pwm3，将 GPIO1_IO04 复用为 PWM3_OUT。
- Makefile：外部模块编译入口。
- alpha_pwm.ko：已编译的 ARM 模块。
- imx6ull-alientek-alpha-v27-emmc.dtb：已更新的板级 DTB。

```sh
cd /home/woody/alpha/woody/driver/pwm
make
# 使用其他内核构建目录：
make KDIR=/path/to/kernel-build CROSS_COMPILE=arm-linux-gnueabihf-
```

默认 KDIR=/home/woody/alpha/woody/linux-7.2.6/out。
需要 CONFIG_PWM、CONFIG_PWM_IMX27、CONFIG_OF、CONFIG_MODULES 和 i.MX pinctrl。
当前 out 已有 CONFIG_PWM=y、CONFIG_PWM_IMX27=y，因此底层 PWM 驱动无需额外加载。
模块必须与板上实际运行内核的配置、版本、Module.symvers 对应。

## 部署设备树

已同步 alpha-pwm.dtsi 到内核 arch/arm/boot/dts/nxp/imx/，并将板级
imx6ull-alientek-alpha-v27-emmc.dts 末尾原蜂鸣器 include 替换为：

```dts
#include "alpha-pwm.dtsi"
```

其他外设配置保留，LCD 背光继续使用 PWM1。
重新编译命令：

```sh
cd /home/woody/alpha/woody/linux-7.2.6
make O=out ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
    nxp/imx/imx6ull-alientek-alpha-v27-emmc.dtb
```

将新 DTB 放到 U-Boot 实际加载的位置，重启开发板。
本目录 DTB 已包含此次修改，可以直接部署。GPIO1_IO04 不得被其他 GPIO 或外设申请。
如果之前已经加载旧模块，可在替换 DTB、重启前执行：

```sh
rmmod alpha_beeper
rmmod pwm_gpio
```

重启后不要再加载旧模块；若曾设置自动加载，也应删除相应配置。
本次未连接开发板，未修改板端启动介质或模块配置。

## 板端控制（root）

将 alpha_pwm.ko 复制到板上：

```sh
insmod ./alpha_pwm.ko
PWM=/sys/bus/platform/devices/alpha-pwm/pwm
cat "$PWM"                     # 默认：1000 5000 0
printf '1000 5000 1\n' > "$PWM"   # 1 kHz，50%，开启
printf '2000 2500 1\n' > "$PWM"   # 2 kHz，25%，开启
printf '2000 2500 0\n' > "$PWM"   # 关闭硬件输出
rmmod alpha_pwm                  # 卸载也会关闭
```

写入格式：`频率Hz 占空比万分比 使能`。

- 频率：1～100000 Hz，这是本驱动设置的接口范围。
- 占空比：0～10000，正常极性，高电平时间占比。1 表示 0.01%，
  5000 表示 50%，5001 表示 50.01%，10000 表示 100%。
- 使能：0 关闭，1 开启。
- 非法格式、越界、额外参数返回 EINVAL；底层硬件错误原样返回。
- 读取得到最近一次成功提交的请求值；受时钟和计数器量化影响，实际值可能有偏差。
- 开启后命令退出不影响输出，由硬件自主运行。
- 关闭表示禁用 PWM，PWM API 不保证禁用后的引脚电平。需要持续低电平时可保持使能
  并写 `1000 0 1`；持续高电平写 `1000 10000 1`。
- 在线修改周期/占空比不保证无毛刺；对波形切换有要求时先关闭再配置开启。

此 consumer 独占 PWM3，不要同时通过 /sys/class/pwm 导出同一通道。
本接口频率范围与旧软件 PWM 不同；PWM chip 编号不固定，本接口无需依赖其编号。

## 万分比接口与实际硬件分辨率

此版本将第二个参数从百分比改为万分比，读写均使用新单位。
旧命令 `1000 50 1` 现在表示 0.50%，50% 应改写为 `1000 5000 1`。
例如 `1000 1234 1` 表示 1 kHz、12.34%，`1000 1 1` 表示 0.01%。
更新已有 PWM3 版本只需替换并重新加载 alpha_pwm.ko，设备树不需要更新。

计算使用 64 位乘法：`duty_ns = period_ns * duty / 10000`，避免 ARM32 溢出，
不足 1 ns 的部分向下取整。接口步进为 1/10000，不代表所有频率下物理输出
都能分辨这个步进。底层 pwm-imx27 驱动还会按时钟和预分频向下量化到整数计数。

设实际 PWM 输入时钟为 F、预分频为 P，每周期有效计数约为 N=F/(P*f)，
占空比硬件步进约为 1/N。要分辨 0.01% 的步进，需要 N 至少为 10000；
即使满足这个条件，也不保证每一个万分比请求都能无误差地表示。
例如仅当实际输入时钟为 66 MHz 时，1 kHz 下当前底层算法选择 P=2，
每周期约 33000 个计数，可以分辨万分比级变化；100 kHz 下只有约 660 个计数，
步进约 0.1515%，无法实现 0.01% 的物理分辨率。
实际时钟应在板上通过 debugfs 的 clk_summary 等方式核实。
高频和高占空比分辨率之间需要权衡；若测量误差不符合需求，应降低频率并用示波器验证。

## 示波器检查与排障

探头接 JP6 PIN20，地夹接 PIN32。写 `1000 5000 1` 后应看到约 1 ms 周期，
高、低各约 500 us；写 `2000 2500 1` 后应看到约 500 us 周期，高电平约 125 us。
无源蜂鸣器或其他负载应使用匹配的驱动电路，PWM 引脚用于逻辑控制。

接口不存在时检查 `dmesg | tail -50`、实际加载的 DTB、
`/sys/bus/platform/devices/alpha-pwm/driver` 和 GPIO1_IO04 的 pinctrl 占用。
若 PWM3 底层驱动配置成模块，先加载内核配套的 pwm-imx27 模块。

已完成 ARM 模块 W=1 编译和 DTB 编译，并反编译核对 PWM3、复用及 consumer 连接。
尚未在开发板上加载或测量实际波形。
