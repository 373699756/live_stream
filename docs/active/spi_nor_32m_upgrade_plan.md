# 32M SPI NOR Flash 分区、烧写、运行与升级完整计划

本文档定义 Hi3516DV300 设备在 32M SPI NOR Flash 上的分区、U-Boot 烧写、Linux 挂载、业务文件规划和 Web 升级实现方案。后续升级程序按本文档实现。

## 1. 最终分区

基于海思默认 `boot + kernel + rootfs` 方式扩展，不拆 `boot`，不在线升级 U-Boot。

| 分区 | 起始地址 | 大小 | 结束地址 | 文件系统/格式 | 挂载点 | 说明 |
| --- | ---: | ---: | ---: | --- | --- | --- |
| `boot` | `0x00000000` | `0x00100000` 1M | `0x00100000` | raw | 无 | U-Boot + env，普通升级禁止写 |
| `kernel` | `0x00100000` | `0x00400000` 4M | `0x00500000` | uImage | 无 | `uImage_hi3516dv300` |
| `rootfs` | `0x00500000` | `0x00C00000` 12M | `0x01100000` | jffs2 | `/` | `rootfs_hi3516dv300_64k.jffs2` |
| `bin` | `0x01100000` | `0x00A00000` 10M | `0x01B00000` | squashfs | `/opt/app` | 主程序、动态库、脚本 |
| `web` | `0x01B00000` | `0x00200000` 2M | `0x01D00000` | squashfs | `/www` | Web 静态资源 |
| `config` | `0x01D00000` | `0x00100000` 1M | `0x01E00000` | jffs2 | `/config` | 用户配置，可单独或联合升级 |
| `data` | `0x01E00000` | `0x00200000` 2M | `0x02000000` | jffs2 | `/data` | 日志、升级状态、运行数据 |

`mtdparts`：

```sh
mtdparts=hi_sfc:1M(boot),4M(kernel),12M(rootfs),10M(bin),2M(web),1M(config),2M(data)
```

Linux 启动后期望：

```text
mtd0 boot
mtd1 kernel
mtd2 rootfs
mtd3 bin
mtd4 web
mtd5 config
mtd6 data
```

`boot` 内部仍包含 U-Boot 主体、U-Boot env 和预留区。SDK 默认 U-Boot env 在 `0x00080000-0x000C0000`，所以普通升级禁止擦写 `boot`，避免破坏 U-Boot env。

## 2. U-Boot 烧写命令

以下命令用于工厂烧录或串口恢复。Web 普通升级不使用 `sf erase` / `sf write`，也不升级 `boot`。

### 2.1 烧写 U-Boot

只用于工厂烧录或串口恢复，普通升级禁止写 `boot`。

```sh
mw.b 0x82000000 0xff 0x100000
tftp 0x82000000 u-boot-hi3516dv300.bin
sf probe 0
sf erase 0x0 0x100000
sf write 0x82000000 0x0 0x100000
```

### 2.2 烧写 kernel

```sh
mw.b 0x82000000 0xff 0x400000
tftp 0x82000000 uImage_hi3516dv300
sf probe 0
sf erase 0x100000 0x400000
sf write 0x82000000 0x100000 0x400000
```

### 2.3 烧写 rootfs

rootfs 从海思默认 27M 调整为 12M。

```sh
mw.b 0x82000000 0xff 0xC00000
tftp 0x82000000 rootfs_hi3516dv300_64k.jffs2
sf probe 0
sf erase 0x500000 0xC00000
sf write 0x82000000 0x500000 0xC00000
```

### 2.4 烧写 bin

`bin.squashfs` 放主程序、动态库、脚本、第三方运行资源。

```sh
mw.b 0x82000000 0xff 0xA00000
tftp 0x82000000 bin.squashfs
sf probe 0
sf erase 0x1100000 0xA00000
sf write 0x82000000 0x1100000 0xA00000
```

### 2.5 烧写 web

```sh
mw.b 0x82000000 0xff 0x200000
tftp 0x82000000 web.squashfs
sf probe 0
sf erase 0x1B00000 0x200000
sf write 0x82000000 0x1B00000 0x200000
```

### 2.6 烧写 config

工厂初始化、单独升级或联合升级时使用。

```sh
mw.b 0x82000000 0xff 0x100000
tftp 0x82000000 config.jffs2
sf probe 0
sf erase 0x1D00000 0x100000
sf write 0x82000000 0x1D00000 0x100000
```

### 2.7 初始化 data

`data` 建议工厂只擦空，不纳入普通升级。

```sh
sf probe 0
sf erase 0x1E00000 0x200000
```

## 3. 启动参数

按海思默认写法补全新分区：

```sh
setenv bootargs 'mem=128M console=ttyAMA0,115200 coherent_pool=2M root=/dev/mtdblock2 rootfstype=jffs2 rw mtdparts=hi_sfc:1M(boot),4M(kernel),12M(rootfs),10M(bin),2M(web),1M(config),2M(data)'
setenv bootcmd 'sf probe 0;sf read 0x82000000 0x100000 0x400000;bootm 0x82000000'
saveenv
reset
```

说明：

- `root=/dev/mtdblock2` 保持海思默认 rootfs 编号。
- `rootfstype=jffs2 rw` 对应 `rootfs_hi3516dv300_64k.jffs2`。
- `bin/web/config/data` 不是 rootfs 的一部分，启动后再挂载。
- `boot` 仍是 1M，内部包含 U-Boot env，普通升级不要擦。

## 4. 文件系统规划

### 4.1 rootfs

rootfs 保留基础系统：

```text
busybox
基础 shell 命令
基础系统库
MTD 支持
启动脚本
最小网络工具
挂载点目录
```

rootfs 中必须预创建：

```sh
mkdir -p /opt/app /www /config /data /tmp/live_stream/upgrade
```

SDK 核查结论：

- `linux-4.9.37/.config` 和 `hi3516dv300_smp_defconfig` 均启用
  `CONFIG_TMPFS=y`，内核支持 tmpfs。
- SDK 默认 `rootfs_scripts/rootfs/etc/fstab` 只把 `/dev` 挂成 tmpfs：
  `tmpfs /dev tmpfs defaults 0 0`。
- SDK 默认 rootfs 中的 `/tmp` 只是普通目录，不是 tmpfs。升级方案要求
  `/tmp/live_stream/upgrade` 位于 RAM，因此必须在启动脚本中显式挂载
  `/tmp` 为 tmpfs/ramfs。

### 4.2 bin

`bin.squashfs` 目录结构建议：

```text
bin_root/
├── bin/live_stream
├── sbin/live_sysupgrade
├── lib/*.so
├── scripts/start_app.sh
├── scripts/stop_app.sh
└── version
```

动态库规划：

- 海思 SDK 基础库、C 运行库、loader 等系统级库放 rootfs `/lib` 或 `/usr/lib`。
- 业务程序依赖的第三方库放 `/opt/app/lib`。
- `live_stream` 主程序放 `/opt/app/bin`。
- `live_sysupgrade` 是一次性升级 helper，放 `/opt/app/sbin`，执行前复制到 `/tmp`。
- 不把业务 so 散放到 rootfs，便于单独升级 `bin`。

启动应用前设置：

```sh
export LD_LIBRARY_PATH=/opt/app/lib:/usr/lib:/lib
export PATH=/opt/app/bin:/opt/app/scripts:/bin:/sbin:/usr/bin:/usr/sbin
```

### 4.3 web

`web.squashfs` 目录结构：

```text
web_root/
├── index.html
├── assets/
└── version
```

挂载后：

```text
/www/index.html
/www/assets/*
/www/version
```

### 4.4 config

`config.jffs2` 目录结构建议：

```text
config_root/
├── app.json
├── network.json
├── users.json
└── device.json
```

`config` 可以单独升级，也可以和 `kernel/bin/web` 联合升级。只要升级
`config`，升级完成后必须重启。`rootfs` 在线升级禁用。

### 4.5 data

`data` 放运行数据：

```text
/data/
├── upgrade_status.json
├── upgrade.log
├── operation.log
└── crash/
```

`data` 不纳入普通升级，避免清除升级日志和运行数据。

## 5. 启动脚本

建议在 rootfs 中新增：

```text
/etc/init.d/S20mount_app
/etc/init.d/S80live_stream
```

### 5.1 `/etc/init.d/S20mount_app`

```sh
#!/bin/sh

mkdir -p /tmp
chmod 1777 /tmp

if ! grep -q " /tmp tmpfs " /proc/mounts && \
   ! grep -q " /tmp ramfs " /proc/mounts; then
    mount -t tmpfs -o size=64m,mode=1777 tmpfs /tmp
fi
chmod 1777 /tmp

mkdir -p /opt/app /www /config /data /tmp/live_stream/upgrade

mount_if_needed() {
    dev="$1"
    dir="$2"
    type="$3"
    opts="$4"

    if grep -q " $dir " /proc/mounts; then
        return 0
    fi

    mount -t "$type" -o "$opts" "$dev" "$dir"
}

mount_if_needed /dev/mtdblock3 /opt/app squashfs ro
mount_if_needed /dev/mtdblock4 /www squashfs ro
mount_if_needed /dev/mtdblock5 /config jffs2 rw
mount_if_needed /dev/mtdblock6 /data jffs2 rw

exit 0
```

### 5.2 `/etc/init.d/S80live_stream`

```sh
#!/bin/sh

case "$1" in
    stop)
        killall live_stream >/dev/null 2>&1 || true
        exit 0
        ;;
esac

export LD_LIBRARY_PATH=/opt/app/lib:/usr/lib:/lib
export PATH=/opt/app/bin:/opt/app/scripts:/bin:/sbin:/usr/bin:/usr/sbin
export LIVE_STREAM_CONFIG_DIR=/config

if [ ! -x /opt/app/bin/live_stream ]; then
    echo "live_stream not found"
    exit 1
fi

cd /
/opt/app/bin/live_stream \
    --config-dir /config \
    --static-root /www \
    >> /data/operation.log 2>&1 &

exit 0
```

如果当前程序不支持 `--static-root`，则需要在应用配置中把 Web 根目录改成 `/www`。

## 6. 升级包方案

本方案不使用 `.img` 包头，不从升级包读取 flash 地址。

采用：

```text
Install + Install.sig + 分区名 + 文件名 + sha256
```

升级包：

```text
upgrade.zip
├── Install
├── Install.sig                # 对 Install 原文做 SHA256/RSA 签名
├── uImage_hi3516dv300
├── bin.squashfs
├── web.squashfs
└── config.jffs2
```

当前 Linux 在线升级包禁止携带额外未声明文件，尤其禁止从升级包执行
`live_sysupgrade`。RAM helper 只从设备已安装的
`/opt/app/sbin/live_sysupgrade` 复制到 `/tmp/live_stream/upgrade/` 后执行。

`Install` 示例：

```json
{
  "Version": "1.0.0",
  "Board": "Hi3516DV300",
  "Flash": "spi-nor-32m",
  "PackageType": "normal",
  "Reboot": true,
  "Commands": [
    {
      "Action": "burn",
      "Partition": "kernel",
      "File": "uImage_hi3516dv300",
      "Sha256": "..."
    },
    {
      "Action": "burn",
      "Partition": "bin",
      "File": "bin.squashfs",
      "Sha256": "..."
    },
    {
      "Action": "burn",
      "Partition": "web",
      "File": "web.squashfs",
      "Sha256": "..."
    },
    {
      "Action": "burn",
      "Partition": "config",
      "File": "config.jffs2",
      "Sha256": "..."
    }
  ]
}
```

允许升级：

```text
kernel
bin
web
config
```

禁止普通升级：

```text
boot
rootfs
data
```

其中：

- `config` 可以单独升级。
- `config` 可以和 `kernel/bin/web` 联合升级。
- 只要升级 `config`，升级完成后必须重启。
- `rootfs` 在线升级暂时禁用。当前 rootfs 仍挂载为 `/`，没有 initramfs、
  recovery 分区或 A/B 回滚能力，强刷 rootfs 的断电和运行时风险不可接受。
- `data` 不纳入普通升级，避免清除升级日志和运行数据。

设备端内置分区表决定实际写入目标：

```text
kernel -> /dev/mtd1
rootfs -> /dev/mtd2
bin    -> /dev/mtd3
web    -> /dev/mtd4
config -> /dev/mtd5
```

## 7. Linux 下升级实现

正式程序不调用命令行 `flash_erase`、`mtd_debug`、`dd`。

Linux 下采用 OpenIPC/OpenWrt 类 `sysupgrade` 模式：

```text
Web/API 上传到 /tmp
    ↓
live_stream 校验升级包签名、payload sha256 和 /proc/mtd
    ↓
web-only 包直接在线写 web 分区
    ↓
非 web-only 包复制 /opt/app/sbin/live_sysupgrade 到 /tmp
    ↓
live_sysupgrade 在 RAM 中重新验签、停服务、解包、擦写 flash、sync、reboot
```

`live_stream` 主进程不直接刷 `kernel/bin/config`。这些分区升级必须由
`/tmp/live_stream/upgrade/live_sysupgrade` 完成，避免主进程在卸载 `/opt/app`、
停止自身时进入不可控状态。`rootfs` 在线升级当前直接拒绝。

升级服务直接使用 MTD ioctl：

```text
open /dev/mtdX
ioctl MEMGETINFO
ioctl MEMERASE
write
fsync
readback
sha256
close
```

写入规则：

| 分区 | Linux 设备 | 升级前动作 | 升级后动作 |
| --- | --- | --- | --- |
| `kernel` | `/dev/mtd1` | helper 已在 RAM 中运行 | 写完强制重启 |
| `rootfs` | `/dev/mtd2` | 禁止 Linux 在线升级 | 无 |
| `bin` | `/dev/mtd3` | 停应用，卸载 `/opt/app` | 写完强制重启 |
| `web` | `/dev/mtd4` | web-only 在线升级时卸载 `/www`；组合升级由 helper 处理 | web-only 重新挂载；组合升级重启 |
| `config` | `/dev/mtd5` | 停应用，卸载 `/config` | 写完强制重启 |
| `data` | `/dev/mtd6` | 禁止普通升级 | 无 |

升级校验顺序：

1. 上传包保存到 `/tmp/live_stream/upgrade/uploads`。
2. 本地路径必须解析到 `/tmp/live_stream/upgrade/uploads/` 下的普通文件。
3. 读取 `Install` 和 `Install.sig`。
4. 使用 `/config/upgrade_public_key.pem` 或内置生产公钥验签 `Install` 原文。
5. 解析 `Install`。
6. 校验 `Board/Flash/PackageType`。
7. 拒绝 `boot/rootfs/data` 和所有未声明 zip entry。
8. 校验所有文件存在、sha256、文件大小和镜像魔数。
9. 校验 `/proc/mtd` 分区名称、大小和擦除块大小。
10. 非 web-only 包校验 `/tmp/live_stream/upgrade` 是 `tmpfs` 或 `ramfs`。
11. 全部校验通过后，才允许擦写任何分区。

`/tmp` 不能仅凭路径名判断，必须通过 `/proc/mounts` 确认为 `tmpfs` 或
`ramfs`。如果不是 RAM 文件系统，非 web-only 升级必须拒绝，避免升级包和
helper 写在 NOR rootfs 上导致擦写自身或空间不足。

helper 运行规则：

- 非 web-only 包只允许复制设备内置 `/opt/app/sbin/live_sysupgrade` 到
  `/tmp/live_stream/upgrade/live_sysupgrade`，不允许执行升级包携带的 helper。
- helper 必须重新解析升级包、重新验签、重新校验 sha256 和 `/proc/mtd`。
- helper 的 staging 目录固定在 `/tmp/live_stream/upgrade/staged`。
- helper 停止 `live_stream` 后继续执行，不依赖 `/opt/app/bin/live_stream`。
- helper 写完 `kernel/bin/config` 或组合包后执行 `sync` 和 `reboot`。

失败规则：

- 校验阶段失败：不写 flash。
- 写入阶段失败：停止后续分区。
- 失败原因写 `/data/upgrade.log`。
- 状态写 `/data/upgrade_status.json`。
- 单分区原地升级没有自动回滚；如果断电或写坏系统分区，需要 UART/U-Boot/TFTP 或烧录器恢复。

## 8. 程序员实施步骤

1. 改 U-Boot bootargs 为新 `mtdparts`。
2. rootfs 中加入 `/opt/app /www /config /data` 挂载点。
3. rootfs 中加入 `S20mount_app` 和 `S80live_stream`。
4. 把业务主程序和业务动态库从 rootfs 移到 `bin.squashfs`。
5. 把 Web 静态资源移到 `web.squashfs`。
6. 配置模板制作成 `config.jffs2`。
7. 应用默认配置目录改为 `/config`。
8. 应用默认 Web 根目录改为 `/www`。
9. 升级状态和日志改为 `/data`。
10. 实现升级包解析。
11. 实现内置分区表。
12. 实现 `/proc/mtd` 校验。
13. 实现 MTD ioctl 擦写。
14. 新增 `live_sysupgrade` helper，并随 `bin.squashfs` 发布到 `/opt/app/sbin`。
15. `web-only` 包保留在线升级和重新挂载。
16. `bin/config/kernel` 和组合包走 RAM helper。
17. 完成强制重启策略。
18. 部署生产升级公钥到 `/config/upgrade_public_key.pem` 或编译进固件。
19. 保留 UART/U-Boot/TFTP 恢复流程。

## 9. 验证项

### 9.1 分区验证

```sh
cat /proc/mtd
```

期望：

```text
mtd0: 00100000 00010000 "boot"
mtd1: 00400000 00010000 "kernel"
mtd2: 00c00000 00010000 "rootfs"
mtd3: 00a00000 00010000 "bin"
mtd4: 00200000 00010000 "web"
mtd5: 00100000 00010000 "config"
mtd6: 00200000 00010000 "data"
```

### 9.2 挂载验证

```sh
mount
```

期望：

```text
/dev/mtdblock2 on / type jffs2 (rw,...)
/dev/mtdblock3 on /opt/app type squashfs (ro,...)
/dev/mtdblock4 on /www type squashfs (ro,...)
/dev/mtdblock5 on /config type jffs2 (rw,...)
/dev/mtdblock6 on /data type jffs2 (rw,...)
```

### 9.3 读写验证

```sh
touch /config/test
touch /data/test
touch /www/test
touch /opt/app/test
```

期望：

```text
/config 可写
/data 可写
/www 不可写
/opt/app 不可写
```

### 9.4 升级验证

必须覆盖：

- 单独升级 `web`。
- 单独升级 `bin`。
- 单独升级 `config`，升级完成后重启。
- 升级 `web + bin`。
- 升级 `kernel + bin + web + config`。
- 普通包写 `boot` 必须拒绝。
- 普通包写 `rootfs` 必须拒绝。
- 普通包写 `data` 必须拒绝。
- 无 `Install.sig`、签名错误、公钥未部署必须拒绝。
- 升级包含未声明文件必须拒绝。
- sha256 错误必须拒绝。
- 文件超过分区大小必须拒绝。
- 镜像魔数错误必须拒绝。
- `/proc/mtd` erase size 不匹配必须拒绝。
- 升级 config 后 `/config` 内容为新包内容。
- 升级 config 后 `/data/upgrade.log` 不丢失。
- 非 web-only 升级时 helper 路径必须位于 `/tmp`，且 `/tmp/live_stream/upgrade` 必须是 tmpfs/ramfs。

## 10. 最终结论

最终方案：

```text
boot 1M
kernel 4M
rootfs 12M
bin 10M
web 2M
config 1M
data 2M
```

升级策略：

```text
允许 Linux 在线升级：kernel/bin/web/config
禁止普通在线升级：boot/rootfs/data
```

实现要求：

- U-Boot 下用 `sf erase` / `sf write` 按物理地址烧写。
- Linux 下正式升级用 sysupgrade/RAM helper + MTD ioctl，不依赖 flash 命令行工具。
- `live_sysupgrade` 必须从 `/tmp/live_stream/upgrade` 运行，且启动脚本必须先把
  `/tmp` 挂成 tmpfs/ramfs。
- rootfs 保持海思默认 jffs2。
- 业务程序和动态库放 `bin.squashfs`。
- 一次性升级 helper 放 `bin.squashfs` 的 `/opt/app/sbin/live_sysupgrade`。
- 升级包不携带、不执行 helper；非 web-only 包只运行设备内置 helper 的
  `/tmp` 副本。
- Web 放 `web.squashfs`。
- 配置放 `config.jffs2`，可单独或联合升级。
- 日志和升级状态放 `/data`。
- 升级包不用 `.img`，不携带 flash 地址。
- 升级包必须携带 `Install.sig`，设备端必须验签后才解析和擦写。
- 设备端内置分区表是唯一地址来源。
- 32M NOR 不做 A/B，断电或刷坏需要串口/U-Boot/TFTP 或烧录器恢复。

## 11. 主机端打包工具

当前工程固定拷贝 SDK 的 PC 端文件系统打包工具到 `tools/pc/`：

```text
tools/pc/mksquashfs
tools/pc/mkfs.jffs2
```

来源：

```text
Hi3516CV500_SDK_V2.0.1.0/osdrv/pub/bin/pc/mkfs.jffs2
Hi3516CV500_SDK_V2.0.1.0/osdrv/tools/pc/squashfs4.3/mksquashfs
```

不能使用 `osdrv/tools/board/mtd-utils/bin/mkfs.jffs2` 作为主机打包工具；
该文件是 ARM 板端 ELF，只能在设备侧运行，不能在 x86-64 开发主机上给
升级包打 `config.jffs2`。

`scripts/package_upgrade.sh` 生成 `config.jffs2` 时优先使用 `tools/pc/mkfs.jffs2`；
生成 squashfs 时优先使用 `tools/pc/mksquashfs`。当前 SDK 预编译的
`osdrv/pub/bin/pc/mksquashfs` 是 x86-64 静态 ELF，但在当前开发主机上实测会
`Floating point exception`；因此使用 SDK 自带
`osdrv/tools/pc/squashfs4.3` 源码重新编译出的 `mksquashfs`。

重编译注意事项：

- `osdrv/tools/pc/squashfs4.3/tmp` 是解压和构建产物，可以删除。
- 现代 GCC 默认 `-fno-common`，老版 squashfs4.3 源码会在链接时报
  `fwriter_buffer` / `bwriter_buffer` multiple definition。
- 修正点必须放在 SDK 顶层
  `osdrv/tools/pc/squashfs4.3/Makefile`，把 squashfs-tools 子 make 的
  `EXTRA_CFLAGS` 从 `-I$(BUILD_DIR)/include` 改为
  `-I$(BUILD_DIR)/include -fcommon`。
- 不能用 `make CFLAGS=...` 覆盖编译参数，否则会丢失 squashfs-tools 自带的
  `-D_GNU_SOURCE`、`-DCOMP_DEFAULT`、`-DGZIP_SUPPORT`、`-DXZ_SUPPORT` 等参数。
- 静态链接阶段出现 `Using 'getpwuid' in statically linked applications`、
  `Using 'getgrgid' in statically linked applications` 是 glibc 静态链接警告，
  不是失败原因。

如需强制指定工具，可通过 `MKSQUASHFS=/path/to/mksquashfs` 或
`MKFS_JFFS2=/path/to/mkfs.jffs2` 覆盖。

生成 `Install` 时 `scripts/package_upgrade.sh` 保持纯 shell，不引入 Python；
字段值来自固定分区名、固定文件名、sha256 和经过白名单限制的版本号。
脚本要求：

- `UPGRADE_SIGN_KEY=/path/to/private_key.pem` 必须指向生产私钥。
- `UPGRADE_PUBLIC_KEY` 默认使用 `configs/upgrade_public_key.pem`，用于打包端
  立即验证 `Install.sig`，防止私钥和设备公钥不匹配。
- 私钥不能提交进仓库，设备端只保存公钥。
- 生成 `config-only` 包时，脚本会把当前公钥写入 `config.jffs2`，避免升级
  配置分区后丢失后续验签所需公钥。
- 当前脚本只生成 `web-only`、`bin-web`、`config-only` 三类包；
  `kernel-rootfs` 和 `full` 因包含 rootfs 在线升级被拒绝。

C++ 侧生成 JSON 状态文件必须使用项目 JSON 模块，不允许手写转义和字符串拼接。
