# ALIENTEK ALPHA i.MX6ULL / WM8960 音频

针对本工作区 Linux 7.2.6，采用标准 ALSA/ASoC：`alpha_audio.c` 是板级
machine driver，复用内核 `sound/soc/codecs/wm8960.c`、FSL SAI 和 SDMA。
音频数据通过 ALSA PCM/DMA 传输，I2C 只用于 Codec 控制。无需额外字符设备。

当前固定 **48000Hz、S16_LE、双声道、I2S**，播放和采集可同时使用。
SAI2 输出 12.288MHz MCLK，WM8960 为 BCLK/LRCLK 主设备，BCLK 为 1.536MHz；
设备树先把 PLL4_AUDIO_DIV 初始化为 786.432MHz，再由 SAI2 分频为 MCLK，
避免沿用引导程序遗留的无效 PLL4 频率。
SAI RX 默认同步到 TX 时钟引脚。设备树启用 shared-lrclk，支持单独录音。
暂不提供其他采样率、自动耳机插拔切换或 WAV 文件解析。

## 原理图核对

依据 driver 中底板 V2.4 PDF 第 2 页 AUDIO（U13）和核心板 V2.0 原理图。

| 信号 | CPU 引脚 / WM8960 连接 |
| --- | --- |
| I2C1 SCL / SDA | UART4_TX_DATA / UART4_RX_DATA，Codec 7 位地址 0x1a |
| SAI2 BCLK / SYNC | JTAG_TDI / JTAG_TDO → BCLK / DACLRC |
| SAI2 TX / RX | JTAG_TRST_B / JTAG_TCK → DACDAT / ADCDAT |
| SAI2 MCLK | JTAG_TMS → MCLK |
| AVDD / DBVDD / DCVDD | VCC3.3A，均为 3.3V |
| SPKVDD1 / SPKVDD2 | VCC_5V 经 L8 滤波 |
| 板载 MIC | MICBIAS 偏置，LINPUT2 正输入、LINPUT1 交流接地负输入 |
| LINE IN | LINE_INL → RINPUT2，LINE_INR → LINPUT3 |
| PHONE | HP_L / HP_R 经 47uF 电容输出，非 capless 模式 |
| 喇叭 | SPK_LP/LN、SPK_RP/RN 差分输出 |

ADCLRC/GPIO1 经 R51 接 AUD_INT，并未接第二路 LRCLK；设置 gpio-cfg 将其作为
GPIO。HP_JD 接 RINPUT3，本实现不启用硬件耳机检测，由脚本显式选输出。
SAI2 会占用 JTAG 引脚，I2C1 会占用 UART4 引脚。

## 文件与构建

- `alpha_audio.c`：板级声卡模块，管理 DAI、时钟、格式约束和 DAPM 端点。
- `alpha-audio.dtsi`：I2C1、Codec、电源、SAI2、pinmux 和板级音频路由。
- `audio_test.c`：直接使用 ALSA 内核 UAPI，无 libasound 编译依赖。
- `audio_ctl.c`：直接使用 ALSA Control UAPI 配置混音器并读回核对，无 libasound
  或 alsa-utils 依赖；支持布尔、整数、枚举和多通道值。
- `audio_setup.sh`：优先调用同目录 audio_ctl，也兼容板端已有的 amixer。
- `audio.config`：移植所需的内核配置片段。
- `audio_diag.sh`：在播放、采集运行时采集时钟、Codec 缓存寄存器和 PCM 状态，
  同时输出实际耗时。默认声卡编号为 1：`sh ./audio_diag.sh 1`。

```sh
cd /home/woody/alpha/woody/driver/audio
make                         # ARM 模块及 ARM 测试程序
make host-test               # 主机编译检查
# 可覆盖 KDIR、ARCH、CROSS_COMPILE、USER_CC
```

默认 KDIR 为相邻内核的 out，模块必须匹配板端正在运行的内核和 Module.symvers。
测试程序为 armhf 动态链接，板端需要 `/lib/ld-linux-armhf.so.3`、libc、libm；
纯静态工具链可用 `make USER_CC='arm-linux-gnueabihf-gcc -static' -B test`。

已将片段同步到内核 `arch/arm/boot/dts/nxp/imx/alpha-audio.dtsi`，并在
`imx6ull-alientek-alpha-v27-emmc.dts` 末尾 include；保留既有 LED、按键和网络。
修改 driver/audio 下片段后，需重新复制并构建设备树：

```sh
cd /home/woody/alpha/woody/linux-7.2.6
cp ../driver/audio/alpha-audio.dtsi arch/arm/boot/dts/nxp/imx/
make O=out ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
    nxp/imx/imx6ull-alientek-alpha-v27-emmc.dtb
cp out/arch/arm/boot/dts/nxp/imx/imx6ull-alientek-alpha-v27-emmc.dtb ../driver/audio/
```

当前 out 配置已具备所需选项，无需重新配置内核。其他配置可参考 audio.config，
使用内核 scripts/kconfig/merge_config.sh 合并后重新构建并安装内核与依赖模块。

## 板端部署

1. 把生成的 DTB 放到 U-Boot **实际读取的路径**，重启确认使用新设备树。
2. 复制 `alpha_audio.ko`、`audio_test`、`audio_ctl`、`audio_setup.sh` 到板端同一目录，
   执行 `chmod +x audio_test audio_ctl`。不需要安装 alsa-utils。
3. 安装匹配内核的模块（推荐标准 modules_install + depmod）。
   本构建 WM8960 已内建；SAI、FSL utils 和 SDMA 是模块。
4. 可选 SDMA 固件路径为板端 `/lib/firmware/imx/sdma/sdma-imx6q.bin`。本板
   普通 SAI DMA 使用 ROM 脚本，缺少外部固件时仍可测试。主机现有文件为
   `/lib/firmware/imx/sdma/sdma-imx6q.bin.zst`；本内核未启用固件压缩，
   应用 `zstd -d` 解压后再复制到板端，不能直接使用 `.zst`。
   本目录已备好解压后的 `sdma-imx6q.bin`，安装后重启可重新加载。

板端 root 执行：

```sh
modprobe imx-sdma
modprobe snd-soc-fsl-sai
insmod ./alpha_audio.ko
cat /proc/asound/cards
cat /proc/asound/pcm
ls /dev/snd
```

若没有模块安装工具，可从本目录复制已备好的依赖模块后，按顺序手动加载：

```sh
insmod ./imx-sdma.ko
insmod ./snd-soc-fsl-utils.ko
insmod ./snd-soc-fsl-sai.ko
insmod ./alpha_audio.ko
```

模块已加载时无需再次 insmod。没有 /dev/snd 时需挂载 devtmpfs 或运行 mdev。
下面用声卡 0、PCM 0 举例；应根据 `/proc/asound/cards` 的 alpha-wm8960
实际编号替换脚本参数和 pcmC 后编号，避免操作其他声卡。

## 播放、录音、回放

```sh
# 耳机 + 板载麦克风；脚本先关闭输出再切换路由
sh ./audio_setup.sh 0 headphone mic
./audio_test tone /dev/snd/pcmC0D0p 3 1000 left
./audio_test tone /dev/snd/pcmC0D0p 3 1000 right
./audio_test tone /dev/snd/pcmC0D0p 5 440 both

# 录音 5 秒，然后回放；录音文件必须不存在
./audio_test record /dev/snd/pcmC0D0c mic.pcm 5
./audio_test play /dev/snd/pcmC0D0p mic.pcm

# 外接无源喇叭；从低音量开始
sh ./audio_setup.sh 0 speaker mic
./audio_test tone /dev/snd/pcmC0D0p 3 440

# LINE IN：输入线路音源，不要接喇叭输出
sh ./audio_setup.sh 0 headphone line
./audio_test record /dev/snd/pcmC0D0c line.pcm 5
./audio_test play /dev/snd/pcmC0D0p line.pcm
```

音量脚本使用较低起始值，正弦波幅度为满幅的 10%。脚本自带 audio_ctl；
若板端已有 amixer，可用 `amixer -c 0 contents` 检查实际控制项。
也可直接执行 `./audio_ctl 1 'Headphone Playback Volume' '110,110'`
设置声卡 1 的耳机音量。喇叭两端均为有源差分输出，
不得把负端接地。板载 MIC 接左 ADC，当前脚本将其复制到左右 PCM 声道；
旧脚本下录音仅左声道有信号。MIC 预设将 Capture Volume 从 32 提至 48，
LINPUT1 Boost 从 1 提至 2，合计增加 19dB 模拟增益；LINE IN 切回独立双声道。
LINE IN 受 PCB 连线影响，采集的
左右声道相对插座标注互换，测试程序保留原始 ADC 顺序。

混音器数值是原始值，并非百分比。当前 DAC Playback 255 对应 0dB，
Headphone Playback 110 对应 -11dB，再加上测试音自身 -20dBFS，
总电平约 -31dBFS（不计模拟负载）。旧脚本使用 DAC 200 和 Headphone 90，
两级叠加衰减 58.5dB，加上测试音后约 -78.5dBFS，可能几乎听不到。

原始文件无头部，格式固定 S16_LE/48k/stereo，每秒 192000 字节，5 秒为
960000 字节。录音先丢弃 3 秒数据以等待麦克风启动稳定，看到
`RECORDING NOW` 后开始说话；这段预热不计入输出文件。正常 5 秒录音会
额外花费约 3 秒预热及设备初始化时间。程序结束同时打印音频帧数换算的时长
和 CLOCK_MONOTONIC 测得的实际耗时；如果两者差别明显，应检查实际采样时钟
和 DMA，不能仅用文件大小认定时长正确。
支持部分帧传输、Ctrl+C/TERM 中断和文件错误检查；遇到
underrun/overrun（EPIPE）直接报错，避免把不连续的数据当作测试成功。
中断录音保留已写入完整帧，退出码 130；正常退出为 0。播放结束 drain 确保
尾部数据输出。`play` 不接受 WAV；可在主机用 ffmpeg 转为指定原始格式。

## 排查与验证范围

板端已确认可以播放正弦波。旧 DTB 下，采集 240000 帧仅耗时 1.384 秒，
播放 240000 帧也仅耗时 1.395 秒。诊断日志显示 PLL4 为 147.456MHz，
低于 clk-pllv3.c 中 AV PLL 的 27 x 24MHz = 648MHz 下限；SAI2 的
12.288MHz 软件报告值不能据此证明实际硬件时钟正确。
当前修订在 SAI2 assigned-clocks 中首先设置 PLL4_AUDIO_DIV 为 786.432MHz，
使用与内核 i.MX6UL EVK 相同的音频根频率；保留原有 PLL3_PFD2 配置。
2026-10-21 用户替换 DTB 后上板日志确认：PLL4 为 786.432MHz，SAI2 MCLK
为 12.288MHz；5 秒播放实际耗时 5.018 秒，12 秒正式采集实际耗时
12.010 秒，两项退出码均为 0。录放速度异常已修复；毫秒级耗时差包含缓冲、
调度等开销，不能用这组数值估算时钟精度。
audio_diag.sh 保留临时录音及日志路径，便于对比。

- 无声卡：检查 dmesg、设备树 include、I2C1 引脚和 0x1a 设备绑定。
- 有声卡但无声：先运行 setup，确认 Headphone/Speaker Switch 与 PCM Playback
  Switch；用示波器检查 MCLK 12.288MHz、BCLK 1.536MHz、LRCLK 48kHz。
- 单独录音失败：核对 shared-lrclk、gpio-cfg、SAI RX 同步模式和 SDMA 固件加载。
- 普通 SAI DMA 可使用 ROM 固件；若安装外部固件，异步加载可能稍晚完成。
- 不要用 i2cget/i2cdump 读取寄存器验证 WM8960；它使用写控制接口，驱动通过
  regmap 缓存寄存器。不要用 i2cset 绕过已绑定的 Codec 驱动。

已完成 ARM 模块 W=1 构建、ARM/主机程序 -Werror 构建、DTB 构建、
驱动 checkpatch（0 错误/0 警告）、脚本语法与 7 组参数/错误设备检查。
audio_ctl 另通过 12 组模拟 Control ioctl 检查，覆盖多通道、枚举、错误输入、
控制项不存在和读回不一致；配置脚本的四种输入/输出组合已验证调用参数。
录音预热和耗时统计通过模拟 PCM 检查：部分帧读取下丢弃 144000 帧、
保留 240000 帧，文件大小 960000 字节，正式录音耗时统计为 5 秒。
上板证据来自用户提供的日志和录音文件；正弦波播放已由用户听音确认。
mic4.pcm 的 5 秒数据中，左声道峰值为 694（约 -33.5dBFS），右声道全零；
据此增加了 MIC 预设增益并复制到双声道。2026-10-21 用户确认新版脚本下
mic5.pcm 录音正常，耳机回放两个声道均有声音；文件大小 960000 字节，
对应 5 秒，左右采样完全一致，峰值 4659，无满幅削顶。
板载单麦克风产生的是双声道相同内容，并非两个独立麦克风的立体声采集。
LINE IN、喇叭输出及同时录放尚未完成板端验证。
编译产物由本目录 .gitignore 排除。
