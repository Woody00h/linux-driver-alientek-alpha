# 构建验证（2026-11-04）

- 使用现有 Linux out/ 配置和 Module.symvers 交叉编译成功。
- 模块内核版本：7.2.6。
- aic_load_fw.ko、aic8800_fdrv.ko 均为 ARM EABI5 32 位目标。
- wpa_supplicant、wpa_cli、wpa_passphrase、usb_modeswitch 均为 ARM EABI5 静态程序。
- readelf 检查 wpa_supplicant 无 INTERP 段。
- 所有新增 shell 脚本通过 sh -n 语法检查。
- 部署包已检查包含固件、模块、工具、配置示例和许可证。

构建警告：上游 rwnx_debugfs.c 的 ACS 信息读取函数有 1888 字节栈帧警告；
静态 glibc 链接 libnl 时提示名称解析相关函数的 NSS 运行时依赖。无线认证与
netlink 配置不依赖通过这些函数解析远程主机名，仍需实板验证运行。

板端验证：水星 UX3 免驱版从 a69c:5721 切换到 2357:0147（AIC8800DC），
加载固件并注册 wlan0，完成 WPA2 认证和 DHCP。经 wlan0 ping 无线网关，
3 发 3 收、0% 丢包。重新获取租约后，路由器显示 alpha-imx6ull。
测试期间 eth0 保持连接，为 NFS 根文件系统提供访问。未验证外网连通性。

源码压缩包哈希记录在 sources/SHA256SUMS。驱动来源和固定 commit 见 README.md。

2026-11-08：确认 out/vmlinux 内嵌版本为 7.2.6，且 out/zImage 与 TFTP
发布的 zImage 字节一致。按此版本重新链接模块，两者 vermagic 均以
7.2.6 开头，部署包不包含旧的带 Git 后缀版本的模块。
