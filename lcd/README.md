# ALPHA i.MX6ULL：ATK-MD0700R 1024×600 LCD

适用于本工作区 Linux 7.2.6、ALPHA V2.4 底板和 40P RGB 接口。
采用内核 DRM/KMS 显示栈：`drivers/gpu/drm/mxsfb/` 驱动 LCDIF，
`drivers/gpu/drm/panel/panel-simple.c` 的 panel-dpi 支持屏幕时序，
`drivers/video/backlight/pwm_bl.c` 控制背光。设备树是本屏的板级驱动适配，
无需另外 insmod 一个占用 LCDIF 寄存器的模块。提供 `/dev/dri/card*`，并通过
DRM fbdev 仿真提供 `/dev/fb0`，供控制台和本目录彩条程序使用。

## 硬件与时序

依据同目录模块手册 V1.2 第 4～6 页和 driver 目录底板 V2.4 原理图第 1、5 页。
模块原理图 MODE 通过 4.7k 上拉，采用 DE 模式。

| 信号 | 板级连接 |
| --- | --- |
| 电源 | 40P pin 1、2：固定 5V，IO 3.3V |
| R0～R7 | LCD_DATA16～23 |
| G0～G7 | LCD_DATA08～15 |
| B0～B7 | LCD_DATA00～07 |
| CLK / DE / HSYNC / VSYNC | LCD_CLK / LCD_ENABLE / LCD_HSYNC / LCD_VSYNC |
| BL，pin 34 | GPIO1_IO08 复用 PWM1，20kHz，正极性 |
| RESET，pin 40 | 板级 RESET，不是 SNVS_TAMPER9 |
| CS，pin 35 | SNVS_TAMPER9 是触摸复位，本适配不占用 |

24 位 RGB888 总线；fbdev 的内存通常是 32 位 XRGB8888，二者无需位数相同。
DATA7/15/23 的 ID 模拟开关连接由底板 VSYNC 控制，不在驱动中读取 ID。

LCDIF 时钟父源显式选择 PLL5_VIDEO_DIV，由 DRM 按显示模式设置像素时钟，
避免依赖 U-Boot 遗留的时钟选择。像素时钟 51.2MHz，水平 1024 + 160（前肩）+ 20（同步）+ 140（后肩）= 1344，
垂直 600 + 12 + 3 + 20 = 635，刷新率约 59.99Hz。
水平/垂直消隐分别为 320 像素、35 行，符合手册 DE 模式典型值。
这些前后肩分配用于 DE 模式，不能直接当成手册 HV 表的前后肩。
DE 高有效，同步低有效，控制器下降沿输出像素、屏幕上升沿采样。
手册没有完整规定采样边沿；该边沿作为初始适配值，仍需实屏验证。
如果你的模块改成了 HV 模式，需要重新核对完整同步时序。

本次仅处理显示与背光。GT911/FT5446 触摸是独立 I2C 输入设备，尚未启用。

## 文件与编译

- `alpha-lcd.dtsi`：屏幕、电源、LCDIF、PWM 背光和引脚配置。
- `lcd.config`：需要的内核配置。
- `lcd_test.c`：无第三方库依赖的 fbdev 八色彩条，支持 16/24/32bpp、stride 和偏移。
- `Makefile`：交叉编译并同步设备树片段，输出测试程序和 DTB。

已经在内核 `arch/arm/boot/dts/nxp/imx/imx6ull-alientek-alpha-v27-emmc.dts`
末尾 include 片段，保留现有板级配置。驱动目录中的 dtsi 是维护源，`make dtb`
将其同步到内核。主 DTS 仍沿用已有 v27 文件名，描述的硬件是 V2.4。

```sh
cd /home/woody/alpha/woody/driver/lcd
make                    # lcd_test 和 imx6ull-alientek-alpha-v27-emmc.dtb
# 当前 out/.config 已包含所需显示选项；换配置时才执行：
make config             # 合并 lcd.config 并 olddefconfig
make kernel             # 输出 ../../linux-7.2.6/out/arch/arm/boot/zImage
```

可覆盖 `KERNEL_SRC`、`KDIR`、`ARCH`、`CROSS_COMPILE`。
测试程序默认 armhf 动态链接，板端需要 `/lib/ld-linux-armhf.so.3` 和 libc。
如板端 libc 不匹配：`make -B lcd_test CC='arm-linux-gnueabihf-gcc -static'`。
不要使用旧版 Linux 4.1.15 的 `display0`/`fsl,lcdif` framebuffer 节点替代本片段。

## 部署和测试

1. 将 DTB 复制到 U-Boot 实际加载的位置，保留旧 DTB 以便回退。
2. 使用具有上述配置的匹配内核；如果板端配置不同，同步新 zImage 和匹配模块。
   所有显示驱动在当前 out 配置中均为内建，无需单独加载 lcd.ko。
3. 将 `lcd_test` 复制到板端，使用新 DTB 启动。

在板端 root shell 执行：

```sh
dmesg | grep -Ei 'mxsfb|lcdif|panel|backlight|drm|fb0'
ls /dev/dri /dev/fb*
cat /sys/class/graphics/fb0/name
cat /sys/class/graphics/fb0/virtual_size
ls /sys/class/backlight
# 本设备树的背光名称是 backlight-lcd，最大亮度索引应为 11。
cat /sys/class/backlight/backlight-lcd/max_brightness
echo 8 > /sys/class/backlight/backlight-lcd/brightness
chmod +x ./lcd_test
./lcd_test /dev/fb0
```

预期从左至右白、黄、青、绿、品红、红、蓝、黑八条色带。程序会覆盖当前画面。
如果控制台光标或文字覆盖彩条，可先停止使用该 framebuffer 的应用；需要时通过串口
临时解绑 fbcon，测试后重新绑定。亮度 `0` 关闭背光，`11` 最亮。

若无 `/dev/fb0`，先查 LCDIF/panel/PWM 的 probe 日志、内核配置、实际 DTB
和 bootargs 中是否禁用了显示或 fbdev。`/dev/dri/card*` 的编号不保证固定。
若背光亮但无图，确认排线、实际加载的 DTB、LCD_PCLK 约 51.2MHz、
HSYNC 约 38.1kHz、VSYNC 约 60Hz，以及屏幕 MODE。
若花屏或闪点，先测时钟和采样边沿；可尝试将 `pixelclk-active` 改为 `1` 后
重新编译 DTB 验证。若颜色错位，检查 RGB 数据线映射，勿直接假设 16bpp 内存布局。
DRM 的 “assuming MEDIA_BUS_FMT_RGB888_1X24” 日志是通用 panel-dpi 的格式回退，
本硬件总线即 RGB888。

## 验证范围

已交叉编译测试程序、构建设备树，并核对当前内核启用所需驱动。
全内核 zImage 重编译已手动停止，本次未生成新的内核镜像；本次变更仅需重编译 DTB。
尚未连接开发板，实际亮屏、PWM 极性和像素采样边沿需要上板验证。

## 纯色检查与控制台光标

将最新的 lcd_test 复制到板端后执行（无需更换 DTB）：

```sh
printf '\033[?25l' > /dev/tty1  # 隐藏 fbcon 光标
./lcd_test /dev/fb0 green     # 全绿
./lcd_test /dev/fb0 magenta   # 全品红
./lcd_test /dev/fb0 bars      # 恢复八色彩条
printf '\033[?25h' > /dev/tty1  # 需要时恢复光标
```

纯色还支持 white、yellow、cyan、red、blue、black。不传颜色保持原来的彩条行为。
用纯色检查原绿色/品红交界位置是否仍有黑线；控制台输出仍可能覆盖测试画面。
