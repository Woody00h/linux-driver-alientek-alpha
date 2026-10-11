# ALIENTEK ALPHA i.MX6ULL LED 驱动

GPIO1_IO03，低电平点亮。驱动使用 platform driver、GPIO descriptor 和 LED class，
不直接映射寄存器，也不依赖全局 GPIO 编号。控制接口是
`/sys/class/leds/alpha:green:status/brightness`，写 `1` 点亮，写 `0` 熄灭。
没有 `/dev/led` 字符设备。加载后默认熄灭，卸载、关机时熄灭。

## 文件

- `alpha_led.c`：Linux 7.2.6 外部内核模块。
- `alpha-led.dtsi`：GPIO、active-low 与 pinctrl 配置。
- `led_test.c`：用户态开关/闪烁测试程序。
- `Makefile`：模块和 ARM 测试程序编译入口。

## 1. 设备树

将 `alpha-led.dtsi` 复制到内核的 `arch/arm/boot/dts/nxp/imx/`，在实际启动所用
板级 DTS 的**末尾**添加：

```dts
#include "alpha-led.dtsi"
```

当前源码中的板级文件为 `imx6ull-alientek-alpha-v27-emmc.dts`。
已将设备树片段复制到内核 DTS 目录，并在该板级 DTS 末尾加入 include；
也已生成并复制更新后的 DTB 到本目录。确保 GPIO1_IO03 没有被其他
`gpio-leds` 节点、GPIO hog 或其他外设占用；同一个引脚只能由一个驱动申请。
引脚配置 `0x10b0` 用于 GPIO 输出，逻辑极性由 `GPIO_ACTIVE_LOW` 指定。

重新编译并部署 DTB（以下命令从内核源码目录执行）：

```sh
make O=/path/to/kernel-build ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
    nxp/imx/imx6ull-alientek-alpha-v27-emmc.dtb
```

编译结果位于构建目录 `arch/arm/boot/dts/nxp/imx/`。将其放到 U-Boot 实际加载的
位置，并重启开发板。只加载模块而不更新设备树，不会创建 LED 接口。

## 2. 编译

必须使用**开发板当前运行内核对应的已构建目录**，包括 `.config`、生成头文件
和 `Module.symvers`。同为 7.2.6 仍可能因配置、LOCALVERSION 或符号版本不同而
不能加载。需要启用 `CONFIG_MODULES`、`CONFIG_OF`、`CONFIG_GPIOLIB`、
`CONFIG_LEDS_CLASS`（`y` 或 `m`）和 i.MX pinctrl/GPIO 支持。

```sh
cd /home/woody/alpha/woody/driver/led
make KDIR=/path/to/kernel-build CROSS_COMPILE=arm-linux-gnueabihf-
```

默认 `KDIR` 已指向 `/home/woody/alpha/woody/linux-7.2.6/out`，在本目录运行
`make` 即可编译。若使用其他构建目录，KDIR 应指向对应输出目录。`modules_prepare` 不会生成用于符号校验的
`Module.symvers`，应先完成对应内核的正式构建。

产物：`alpha_led.ko`、`led_test`。单独编译测试程序：`make test`。
默认编译器适合 armhf 用户空间；若根文件系统使用其他 ABI/libc，请指定相应
`CROSS_COMPILE`。测试程序动态链接，需要板端匹配的动态加载器和 libc。

## 3. 板端测试

将模块和测试程序复制到开发板，在板上以 root 执行：

```sh
# 若 LED class 是模块，先加载内核配套的 led-class 模块
# modprobe led-class
insmod ./alpha_led.ko
ls /sys/class/leds/alpha:green:status
./led_test on
./led_test off
./led_test blink 500 10
rmmod alpha_led
```

`blink 500 10` 表示亮 500 ms、灭 500 ms，重复 10 个完整周期，结束后熄灭。
闪烁期间 Ctrl+C 或 SIGTERM 也会熄灭后退出。程序先禁用 LED trigger，避免
内核触发器和手动控制竞争；没有编入 trigger 支持时也可使用。
可传自定义 sysfs 目录，例如 `./led_test on /sys/class/leds/another-led`。

也可直接控制：

```sh
echo none > /sys/class/leds/alpha:green:status/trigger # 有该文件时执行
echo 1 > /sys/class/leds/alpha:green:status/brightness
echo 0 > /sys/class/leds/alpha:green:status/brightness
```

## 4. 代码解释

### 4.1 从测试程序到硬件的控制流程

```text
led_test 写 brightness（1 或 0）
    → LED 子系统解析亮度值
    → 调用 alpha_led_set()
    → gpiod_set_value_cansleep() 设置逻辑 GPIO 值
    → GPIO 子系统依据 GPIO_ACTIVE_LOW 转换电平
    → GPIO1_IO03 输出低/高电平，LED 点亮/熄灭
```

测试程序只使用 sysfs 接口，不需要知道寄存器地址或 GPIO 编号。
LED 子系统负责创建 `brightness`、`max_brightness` 等属性；驱动负责把亮度
请求转换成 GPIO 操作。`blink` 的周期由用户态程序控制，驱动本身不创建闪烁定时器。

### 4.2 设备树：描述引脚与匹配关系

`alpha-led.dtsi` 中的关键配置如下：

```dts
compatible = "alientek,alpha-led";
pinctrl-names = "default";
pinctrl-0 = <&pinctrl_alpha_led>;
led-gpios = <&gpio1 3 GPIO_ACTIVE_LOW>;
```

- `compatible` 与驱动的 `alpha_led_match[]` 对应。内核根据设备树创建 platform
  设备，驱动注册后通过这个字符串匹配，匹配成功才执行 `alpha_led_probe()`。
- `pinctrl-names` 和 `pinctrl-0` 指定默认引脚配置。驱动核心在 probe 前选择
  默认状态，把引脚复用为 GPIO。
- `led-gpios` 指向 GPIO1 控制器的第 3 号引脚，即 GPIO1_IO03。
  `GPIO_ACTIVE_LOW` 表示逻辑有效状态对应低电平。
- `MX6UL_PAD_GPIO1_IO03__GPIO1_IO03` 选择 GPIO 功能；后面的 `0x10b0` 是
  pad 电气属性配置，涉及驱动强度、上下拉等。它与 GPIO 输出值是不同的配置。

`devm_gpiod_get(dev, "led", ...)` 中的 `"led"` 是 GPIO 消费者名称，GPIO 子系统
据此查找设备树的 `led-gpios` 属性。改动其中一个名称时，另一个也要同步。

### 4.3 驱动：数据结构和 probe 初始化

```c
struct alpha_led {
    struct gpio_desc *gpio;
    struct led_classdev cdev;
};
```

`gpio` 保存申请到的 GPIO 描述符，其中包含控制器、引脚及极性等信息。
`cdev` 是 LED 子系统需要的设备描述，保存 LED 名称、亮度范围和操作回调。
把两者放入同一个结构体，可以在回调中找到同一设备的 GPIO。

`alpha_led_probe()` 的初始化顺序是：

1. `devm_kzalloc()` 分配并清零设备私有数据。
2. `devm_gpiod_get(dev, "led", GPIOD_OUT_LOW)` 申请 GPIO，并设置为输出、
   初始逻辑值为 0。这里的 `LOW` 表示**逻辑 0**；因为配置了 active-low，
   实际输出是高电平，LED 初始熄灭。
3. `devm_add_action_or_reset()` 注册收尾关灯动作。如果注册失败，会立即执行
   该动作；如果注册成功，解绑或后续 probe 失败时会自动执行。
4. 配置 `cdev`：名称为 `alpha:green:status`，最大亮度为 1，初始亮度为
   `LED_OFF`，亮度回调为 `alpha_led_set()`。这里的最大亮度 1 表示只有
   开、关两种状态，没有 PWM 调光。
5. `LED_CORE_SUSPENDRESUME` 让 LED 核心在挂起时关闭 LED，并在恢复时恢复
   保存的亮度。`platform_set_drvdata()` 保存私有数据，供 shutdown 获取。
6. `devm_led_classdev_register()` 注册 LED class 设备，从而创建 sysfs 接口。

带 `devm_` 的资源由设备管理机制自动释放，因此这里没有手工实现 `remove()`。
申请 GPIO 失败时，`IS_ERR()` 检查错误指针，`PTR_ERR()` 取出错误码，
`dev_err_probe()` 记录错误并返回；它也能正确处理 GPIO 控制器尚未就绪时的
`-EPROBE_DEFER`，让内核稍后重试 probe。

### 4.4 亮度回调与 active-low

```c
struct alpha_led *led = container_of(cdev, struct alpha_led, cdev);
gpiod_set_value_cansleep(led->gpio, brightness != LED_OFF);
```

LED 核心传入的是嵌在 `struct alpha_led` 中的 `cdev` 指针。
`container_of()` 根据成员在结构体中的偏移，找回外层 `struct alpha_led`，
随后就能取得 GPIO 描述符。

`brightness != LED_OFF` 把亮度转为逻辑开关值，极性转换交给 GPIO 子系统：

| 写入 brightness | GPIO 逻辑值 | GPIO1_IO03 实际电平 | LED 状态 |
| --- | --- | --- | --- |
| `0` | 0 | 高 | 熄灭 |
| `1` | 1 | 低 | 点亮 |

因此代码中不需要再次取反；手动取反反而会把开关语义颠倒。
`gpiod_set_value_cansleep()` 允许 GPIO 操作睡眠，所以使用 LED 的
`brightness_set_blocking` 回调，不能将同样实现放到要求不能睡眠的
`brightness_set` 回调中。

### 4.5 模块注册、卸载与关机

`alpha_led_driver` 将 `probe`、`shutdown` 和设备树匹配表关联起来。
`module_platform_driver()` 生成模块初始化/退出代码，在加载时注册 platform
驱动，在卸载时注销驱动。`MODULE_DEVICE_TABLE(of, ...)` 将匹配信息导出为
模块别名，供支持模块自动加载的用户空间使用；手动 `insmod` 也能完成匹配。

设备管理资源按注册顺序的逆序释放。关灯动作先于 LED class 注册，因此
卸载时先注销 LED class、停止触发器和软件闪烁并完成待执行的亮度工作，
再执行 `alpha_led_off()`，最后释放 GPIO 和私有数据。
这样不会在 GPIO 释放之后仍由 LED 回调访问它。

`alpha_led_shutdown()` 用于关机或重启：先通过
`devm_led_classdev_unregister()` 注销 LED class，再显式设置逻辑 0。
注销操作也会移除对应的 devm 资源记录，避免后续重复注销。

### 4.6 测试程序：参数、写入和闪烁

`main()` 支持三种命令：`on`、`off`、`blink INTERVAL_MS COUNT`，还可以传入
自定义 LED sysfs 目录。`number()` 用 `strtoul()` 解析正整数，同时检查非法
字符、溢出、0 和上限；间隔为 1～3600000 ms，次数为 1～1000000。

`write_attr()` 把目录和属性名拼成路径，用 `open()`、`write()`、`close()`
向 sysfs 写入文本。例如开灯写入 `"1\n"`，关灯写入 `"0\n"`。
它检查路径长度、文件打开失败、短写及关闭失败；`write()` 被信号中断并返回
`EINTR` 时重试。失败后保留相应 `errno`，供 `perror()` 打印原因。

程序控制亮度前先向 `trigger` 写入 `none`，解除已有内核触发器。
没有编入 trigger 支持时可能没有此文件，所以忽略该属性写入的 `ENOENT`；
之后仍必须成功打开 `brightness`。这也解释了设备树节点缺失时，程序可能
先跳过 trigger 写入，再报 `LED on: No such file or directory`。

闪烁循环每轮执行：写 1 → 等待 → 写 0 → 等待。
`delay()` 将毫秒拆成 `timespec` 的秒和纳秒，使用 `nanosleep()` 等待；
被信号打断时，在未要求停止的情况下使用剩余时间继续等待。
这属于用户态定时，会受到系统调度影响，适用于功能测试而非精确定时。

信号处理函数 `stop_handler()` 只设置 `volatile sig_atomic_t stopped` 标志。
实际 GPIO 控制留给主循环完成，不在信号处理函数中执行文件 I/O。
收到 SIGINT（Ctrl+C）或 SIGTERM 后，循环结束，并最终再次写 0 关灯。
正常结束和闪烁写入失败也会尝试最终关灯；SIGKILL 无法被捕获，不会执行此收尾。
`on` 命令成功退出后则保留点亮状态，直到下一次控制或驱动卸载。

### 4.7 Makefile：内核模块和用户程序分别编译

当内核构建系统进入本目录并设置 `KERNELRELEASE` 时，执行：

```make
obj-m := alpha_led.o
```

这告诉 Kbuild 将 `alpha_led.c` 构建为外部模块 `alpha_led.ko`。
普通执行 `make` 时走另一分支：`module` 目标通过
`make -C $(KDIR) M=$(CURDIR) ... modules` 调用内核构建系统；`test` 目标
使用交叉编译器单独编译用户程序。两者分别使用内核接口和用户空间 libc。

`KDIR` 指向内核输出目录 `out`，`ARCH=arm` 指定内核目标架构，
`CROSS_COMPILE=arm-linux-gnueabihf-` 指定工具链前缀。
`host-test` 目标使用主机编译器生成 `led_test_host`，便于模拟 sysfs 验证。
用户程序启用 `-Wall -Wextra -Werror`，将编译警告视为错误。

## 排查

- 无 sysfs 节点：检查新 DTB 是否实际加载、`dmesg` 是否 probe 失败。
- `cannot acquire LED GPIO` / busy：检查重复 GPIO 消费者和 pinctrl 配置。
- `Invalid module format`：核对 `uname -r`、模块 vermagic 和内核构建配置。
- `Unknown symbol`：检查 LED class 依赖已加载，并使用正确的 Module.symvers。
- 测试程序不能执行：检查 ARM ABI、动态加载器/libc 和文件执行权限。

主机编译和模拟 sysfs 只能验证程序与源码接口，实际电平和 LED 闪烁需要板端验证。

## 本次验证

- 使用本地 Linux 7.2.6 源码、临时 `imx_v6_v7_defconfig` 构建目录完成
  `modules_prepare`，驱动 `alpha_led.o` 在 `W=1` 下编译通过。
- `checkpatch.pl` 检查驱动：0 errors、0 warnings。
- ARM 测试程序和主机测试程序均在 `-Wall -Wextra -Werror` 下编译通过。
- 模拟 sysfs 验证开/关、闪烁、SIGINT 收尾关灯、缺少 trigger、参数错误和路径错误。
- 将片段加入现有板级 DTS 的临时副本，预处理和 `dtc` 编译通过。

使用 `/home/woody/alpha/woody/linux-7.2.6/out` 中的实际配置、生成头文件和
`Module.symvers`，已在 `W=1` 下完整编译生成 `alpha_led.ko`，包括 MODPOST
符号版本检查。内核 release 为 `7.2.6`。

构建提示编译器版本不同：内核使用 GCC 13.3，本机使用 GCC 15.2；编译链接成功，
用户已在开发板成功加载模块，确认 `/proc/device-tree/alpha-led` 和
`/sys/class/leds/alpha:green:status` 存在，执行 `led_test blink 500 10` 后 LED
正常闪烁。卸载关灯、关机和挂起恢复行为尚未单独进行板端验证。

板端若出现 `LED on: No such file or directory`，首先检查
`ls /sys/class/leds` 和 `ls /proc/device-tree/alpha-led`。模块加载成功并不代表
设备已绑定；需要将新 DTB 部署到 U-Boot 实际加载的位置并重启。
