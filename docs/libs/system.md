# system

## 命名迁移

本模块命名迁移遵循`docs/refactor/README.md` 的命名规则。后续目录、静态库、public header、接口类、Options/Dependencies/Stats、工厂函数和变量名只按该基线迁移；本文件中的旧 `_service`、`stream_*`、`MetaRtc*` 或 `Yang*` 名称仅表示迁移前名称、历史说明或明确允许保留的协议概念。HTTP REST 路径、配置 schema、Web DTO 和 ONVIF 返回路径可以随完全重构同步迁移；变更必须在同一任务内更新调用方、配置样例和文档，不保留旧兼容适配。

## 模块定位

`system` 是设备系统运维模块，物理拥有系统状态/动作、时间同步和升级三组能力。
它继续通过独立 public interface 暴露 `ISystem`、`ITime`、`IUpgrade`，避免把三类
业务揉成一个大接口。平台动作分别通过 `ISystemPlatform`、`ITimePlatform`、
`IUpgradePlatform` 注入，Linux/板端实现仍由 `app/platform/linux` 提供。

## 总体框架图

```mermaid
flowchart LR
  HTTP[http system handlers] --> System[system]
  HTTP --> Time[time handlers]
  HTTP --> Upgrade[upgrade handlers]
  System --> Platform[ISystemPlatform]
  Time --> TimePlatform[ITimePlatform]
  Upgrade --> UpgradePlatform[IUpgradePlatform]
  Platform --> Linux[/proc /sys reboot etc]
  TimePlatform --> Linux
  UpgradePlatform --> Linux
  System --> Logger[logger]
  System --> Event[event]
  Time --> Logger
  Time --> Event
  Upgrade --> Logger
  Upgrade --> Event
```

## 核心职责

- 查询设备状态、资源状态和基础系统信息。
- 处理 reboot、factory reset 等系统管理动作。
- 输出操作日志和系统状态事件。
- 加载和应用时间/NTP/浏览器登录校时配置，发布 `kTimeChanged`。
- 管理升级校验、准备、写入、等待重启、取消等状态，发布
  `kUpgradeProgressChanged`。
- 拥有 `upgrade_package` 包格式、manifest 和解包校验逻辑；真正写 flash
  仍通过 `IUpgradePlatform` 完成。

## 接口归属

public API 在 `system.h`、`time_api.h`、`upgrade.h` 和 `upgrade_package.h`。
HTTP `/api/system/*`、`/api/time/*`、`/api/upgrade/*` 路由归 `http`，
页面展示归 Web。

## 状态与资源模型

系统状态来自 `ISystemPlatform` 对 Linux/板端信息的即时查询。reboot、factory reset
等动作必须经权限校验和操作日志记录；模块不缓存媒体 pipeline 状态。

时间配置仍使用 `time` scope。`browser_sync_on_login` 控制 Web 登录后是否用浏览器
当前 Unix 毫秒时间同步设备时间，`manual_sync_allowed=false` 时浏览器校时也会被关闭。

升级包上传后先落入临时目录，校验完成前不得写 flash。升级状态必须足够支撑 Web 展示，
取消只对尚未进入不可中断写入阶段的流程生效。普通在线升级不写 `boot` 分区。

## 升级分区

Hi3516DV300 设备按 32M SPI NOR 固定分区运行，升级模块以内置分区表作为唯一写入
目标，不从升级包读取 flash 地址。

| 分区 | 地址范围 | 大小 | Linux 设备 | 挂载点 | 格式 | 在线升级 |
| --- | --- | ---: | --- | --- | --- | --- |
| `boot` | `0x00000000-0x00100000` | 1M | `/dev/mtd0` | 无 | raw | 禁止 |
| `kernel` | `0x00100000-0x00500000` | 4M | `/dev/mtd1` | 无 | uImage | RAM helper |
| `rootfs` | `0x00500000-0x01100000` | 12M | `/dev/mtd2` | `/` | jffs2 | 禁止 |
| `bin` | `0x01100000-0x01b00000` | 10M | `/dev/mtd3` | `/opt/app` | squashfs | RAM helper |
| `web` | `0x01b00000-0x01d00000` | 2M | `/dev/mtd4` | `/www` | squashfs | 支持 |
| `config` | `0x01d00000-0x01e00000` | 1M | `/dev/mtd5` | `/config` | jffs2 | RAM helper |
| `data` | `0x01e00000-0x02000000` | 2M | `/dev/mtd6` | `/data` | jffs2 | 禁止 |

`mtdparts` 固定为：

```text
mtdparts=hi_sfc:1M(boot),4M(kernel),12M(rootfs),10M(bin),2M(web),1M(config),2M(data)
```

板端首次烧写或串口恢复时，U-Boot 启动参数按下列分区写入：

```sh
setenv bootargs 'mem=128M console=ttyAMA0,115200 coherent_pool=2M root=/dev/mtdblock2 rootfstype=jffs2 rw mtdparts=hi_sfc:1M(boot),4M(kernel),12M(rootfs),10M(bin),2M(web),1M(config),2M(data)'
setenv bootcmd 'sf probe 0;sf read 0x82000000 0x100000 0x400000;bootm 0x82000000'
saveenv
```

`root=/dev/mtdblock2` 对应 `rootfs`，`bin`、`web`、`config`、`data` 由启动脚本后续
挂载。`boot` 包含 U-Boot 和 env，普通 Web 升级永远不得擦写。`rootfs` 当前挂载为
`/`，没有 recovery 分区、initramfs 或 A/B 回滚能力，Linux 在线升级必须拒绝。
`data` 保存运行日志、操作日志和升级状态，不纳入普通升级。

## 只有 boot 的开发板首烧计划

如果开发板 flash 里当前只有 `boot` 可用，也就是只能进入 U-Boot，Linux、Web 和
`live_stream` 都还没起来，这时不能使用 Web 上传 `upgrade.zip`。正确流程是先走
U-Boot/TFTP 把整套 Linux 运行环境烧到固定分区，确认 Linux 能启动、分区能挂载、业务能
运行后，再把后续版本更新切换到 Web 升级流程。

完整计划如下：

1. PC 侧准备首烧镜像。
   - `kernel`：准备 `uImage_hi3516dv300`，来自内核/SDK 构建产物。
   - `rootfs`：准备 `rootfs_hi3516dv300_64k.jffs2`，rootfs 内必须包含
     `scripts/rootfs/etc/init.d/S20mount_app` 和 `S80live_stream` 对应的启动逻辑。
   - `bin` 和 `web`：执行 `bin-web` 发布，得到 `bin.squashfs` 和 `web.squashfs`。
   - `config`：执行 `config-only` 发布，得到 `config.jffs2`。
   - `data`：首烧时不需要镜像，U-Boot 下擦空即可。

   示例：

   ```sh
   UPGRADE_SIGN_KEY=/secure/upgrade_private_key.pem \
   UPGRADE_PUBLIC_KEY=configs/upgrade_public_key.pem \
     make release RELEASE_DIR=release-first-bin-web \
       RELEASE_VERSION=1.0.0 RELEASE_PROFILE=bin-web

   UPGRADE_SIGN_KEY=/secure/upgrade_private_key.pem \
   UPGRADE_PUBLIC_KEY=configs/upgrade_public_key.pem \
     make release RELEASE_DIR=release-first-config \
       RELEASE_VERSION=1.0.0 RELEASE_PROFILE=config-only
   ```

   首烧用到的是 `release-first-bin-web/flash/bin.squashfs`、
   `release-first-bin-web/flash/web.squashfs` 和
   `release-first-config/flash/config.jffs2`。`release/flash/upgrade.zip` 只给
   Linux/Web 升级使用，不给 U-Boot 使用。`config.jffs2` 里的
   `upgrade_public_key.pem` 必须和后续发布包使用的私钥匹配，否则设备启动后 Web
   升级验签会失败。

2. 把首烧镜像放到 TFTP 服务器目录。
   - `uImage_hi3516dv300`
   - `rootfs_hi3516dv300_64k.jffs2`
   - `bin.squashfs`
   - `web.squashfs`
   - `config.jffs2`

3. U-Boot 侧配置网络并确认能访问 TFTP 服务器。
   - 配置 `ipaddr`、`serverip`、`gatewayip`、`netmask`。
   - 用 `ping ${serverip}` 验证网络。
   - 如果当前 `boot` 能正常进入 U-Boot，不擦写 `boot`；`boot` 是这块板最后的恢复入口。

4. U-Boot 侧逐分区烧写。
   - 烧 `kernel` 到 `0x00100000-0x00500000`。
   - 烧 `rootfs` 到 `0x00500000-0x01100000`。
   - 烧 `bin` 到 `0x01100000-0x01b00000`。
   - 烧 `web` 到 `0x01b00000-0x01d00000`。
   - 烧 `config` 到 `0x01d00000-0x01e00000`。
   - 擦空 `data` 的 `0x01e00000-0x02000000`。

   这些命令见下一节 “U-Boot TFTP 烧写”。U-Boot 阶段按 flash 偏移写原始分区镜像，
   不存在挂载动作。

5. 写入启动参数并重启。
   - `bootargs` 必须包含完整 `mtdparts`。
   - `root=/dev/mtdblock2 rootfstype=jffs2 rw` 指向 `rootfs`。
   - `bootcmd` 从 `0x00100000` 读取 4M kernel 到 DDR，然后 `bootm`。
   - `saveenv` 后 `reset`。

6. 首次 Linux 启动后做分区和挂载检查。

   ```sh
   cat /proc/mtd
   mount
   grep ' /tmp ' /proc/mounts
   ls -l /opt/app/bin/live_stream /opt/app/sbin/live_sysupgrade
   ls -l /www/index.html
   ls -l /config/upgrade_public_key.pem
   ls -ld /data
   ```

   期望结果是 `/dev/mtdblock3` 挂到 `/opt/app`，`/dev/mtdblock4` 挂到 `/www`，
   `/dev/mtdblock5` 挂到 `/config`，`/dev/mtdblock6` 挂到 `/data`，并且 `/tmp`
   是 `tmpfs` 或 `ramfs`。

7. 确认业务和 Web 升级入口。
   - `live_stream` 从 `/opt/app/bin/live_stream` 启动。
   - 静态页面从 `/www` 提供。
   - 配置从 `/config` 读取。
   - 日志写入 `/data/operation.log`。
   - `/config/upgrade_public_key.pem` 存在后，后续 Web 升级才能验签。

8. 后续版本更新才使用 Web 升级。
   - Web 资源更新：发布 `web-only`，上传 `upgrade.zip`，主进程在线写 `web`。
   - 主程序和 Web 更新：发布 `bin-web`，上传 `upgrade.zip`，走 RAM helper 写
     `bin` 和 `web` 后重启。
   - 配置更新：发布 `config-only`，上传 `upgrade.zip`，走 RAM helper 写 `config`
     后重启。

9. 首烧或启动失败时回到 U-Boot 恢复。
   - 进不了 kernel：优先检查 `kernel` 烧写、`bootcmd` 和 kernel 偏移。
   - kernel 起了但挂不上 `/`：检查 `rootfs` 镜像、`root=/dev/mtdblock2` 和
     `mtdparts`。
   - 应用不存在或起不来：检查 `bin.squashfs` 和 `/opt/app` 挂载。
   - Web 页面不存在：检查 `web.squashfs` 和 `/www` 挂载。
   - 后续 Web 升级验签失败：检查 `config.jffs2` 中的
     `/config/upgrade_public_key.pem` 是否和发布私钥匹配。

只有一个 `boot` 的开发板没有自动回滚能力，`boot` 就是最后恢复锚点。普通发布流程不写
`boot`、不在线写 `rootfs`；需要升级 `boot` 或放开 `rootfs` 在线升级前，必须先设计
独立 recovery、A/B 或外部烧录恢复策略。

## U-Boot TFTP 烧写

U-Boot 串口/TFTP 是工厂烧录或系统损坏后的恢复路径，不是 Web 在线升级路径。这个阶段
Linux 还没有启动，所以没有 `/dev/mtdX`、`/dev/mtdblockX`，也没有 `/opt/app`、
`/www`、`/config` 这些挂载点。U-Boot 只做三件事：从 TFTP 服务器把镜像下载到 DDR，
按 SPI NOR 偏移擦除，再把 DDR 里的内容写到 flash 偏移。

串口恢复前先配置网络，地址按现场环境替换：

```sh
setenv ipaddr 192.168.1.64
setenv serverip 192.168.1.10
setenv gatewayip 192.168.1.1
setenv netmask 255.255.255.0
ping ${serverip}
```

烧写命令使用 `0x82000000` 作为 DDR 临时加载地址。先用 `mw.b` 把整段 buffer 填成
`0xff`，再 `tftp` 下载镜像；如果镜像小于分区，剩余部分会以 NOR 擦除态 `0xff`
补齐。`sf erase` 和 `sf write` 使用固定分区偏移和分区大小：

```sh
# kernel -> 0x00100000, 4M
mw.b 0x82000000 0xff 0x400000
tftp 0x82000000 uImage_hi3516dv300
sf probe 0
sf erase 0x100000 0x400000
sf write 0x82000000 0x100000 0x400000

# rootfs -> 0x00500000, 12M
mw.b 0x82000000 0xff 0xc00000
tftp 0x82000000 rootfs_hi3516dv300_64k.jffs2
sf probe 0
sf erase 0x500000 0xc00000
sf write 0x82000000 0x500000 0xc00000

# bin -> 0x01100000, 10M
mw.b 0x82000000 0xff 0xa00000
tftp 0x82000000 bin.squashfs
sf probe 0
sf erase 0x1100000 0xa00000
sf write 0x82000000 0x1100000 0xa00000

# web -> 0x01b00000, 2M
mw.b 0x82000000 0xff 0x200000
tftp 0x82000000 web.squashfs
sf probe 0
sf erase 0x1b00000 0x200000
sf write 0x82000000 0x1b00000 0x200000

# config -> 0x01d00000, 1M
mw.b 0x82000000 0xff 0x100000
tftp 0x82000000 config.jffs2
sf probe 0
sf erase 0x1d00000 0x100000
sf write 0x82000000 0x1d00000 0x100000
```

`boot` 分区只在工厂首烧或 U-Boot 损坏恢复时写，普通版本升级不要写：

```sh
mw.b 0x82000000 0xff 0x100000
tftp 0x82000000 u-boot-hi3516dv300.bin
sf probe 0
sf erase 0x0 0x100000
sf write 0x82000000 0x0 0x100000
```

恢复完成后写入启动参数并重启：

```sh
setenv bootargs 'mem=128M console=ttyAMA0,115200 coherent_pool=2M root=/dev/mtdblock2 rootfstype=jffs2 rw mtdparts=hi_sfc:1M(boot),4M(kernel),12M(rootfs),10M(bin),2M(web),1M(config),2M(data)'
setenv bootcmd 'sf probe 0;sf read 0x82000000 0x100000 0x400000;bootm 0x82000000'
saveenv
reset
```

U-Boot 只能烧单个分区镜像，不能直接消费 `release/flash/upgrade.zip`。`upgrade.zip`
是 Linux/Web 升级包，里面的签名校验、manifest 解析和 MTD 写入由 `system` 模块和
`live_sysupgrade` 完成。

## Linux 分区挂载

Linux 启动时，内核根据 `bootargs` 里的 `mtdparts` 创建分区设备：

```text
/dev/mtd0       boot      raw，不挂载
/dev/mtd1       kernel    uImage，不挂载
/dev/mtd2       rootfs    通过 root=/dev/mtdblock2 挂载为 /
/dev/mtd3       bin       /dev/mtdblock3 -> /opt/app
/dev/mtd4       web       /dev/mtdblock4 -> /www
/dev/mtd5       config    /dev/mtdblock5 -> /config
/dev/mtd6       data      /dev/mtdblock6 -> /data
```

`rootfs` 是内核启动过程挂载的根文件系统；其余分区由 rootfs 内的启动脚本挂载。当前
仓库提供的脚本是 `scripts/rootfs/etc/init.d/S20mount_app`：

```sh
mkdir -p /tmp
chmod 1777 /tmp

if ! grep -q " /tmp tmpfs " /proc/mounts && \
   ! grep -q " /tmp ramfs " /proc/mounts; then
  mount -t tmpfs -o size=64m,mode=1777 tmpfs /tmp
fi

mkdir -p /opt/app /www /config /data /tmp/live_stream/upgrade
mount -t squashfs -o ro /dev/mtdblock3 /opt/app
mount -t squashfs -o ro /dev/mtdblock4 /www
mount -t jffs2 -o rw /dev/mtdblock5 /config
mount -t jffs2 -o rw /dev/mtdblock6 /data
```

实际脚本里用 `mount_if_needed` 包一层，已挂载时不重复挂载。`/tmp` 必须是 tmpfs 或
ramfs，因为系统升级 helper 会从 `/opt/app/sbin/live_sysupgrade` 复制到
`/tmp/live_stream/upgrade/live_sysupgrade` 后执行；如果 `/tmp/live_stream/upgrade`
落在 flash 文件系统上，系统升级准备阶段会拒绝。

挂载完成后，`scripts/rootfs/etc/init.d/S80live_stream` 设置运行环境并启动业务：

```sh
export LD_LIBRARY_PATH=/opt/app/lib:/usr/lib:/lib
export PATH=/opt/app/bin:/opt/app/scripts:/bin:/sbin:/usr/bin:/usr/sbin
export LIVE_STREAM_CONFIG_DIR=/config

/opt/app/bin/live_stream \
  --config-dir /config \
  --static-root /www \
  >> /data/operation.log 2>&1 &
```

因此分区和文件路径的关系是：程序从 `/opt/app` 运行，Web 静态资源从 `/www` 提供，
配置从 `/config` 读取，日志和升级状态写 `/data`。

`bin.squashfs` 承载 `/opt/app/bin/live_stream`、`/opt/app/sbin/live_sysupgrade`、
业务动态库和脚本；`web.squashfs` 承载 Web 静态资源；`config.jffs2` 承载运行配置、
认证用户配置和 `/config/upgrade_public_key.pem`。

## 发布打包脚本

发布入口是仓库根目录的 `make release`：

```sh
UPGRADE_SIGN_KEY=/secure/upgrade_private_key.pem \
  make release RELEASE_VERSION=1.2.3 RELEASE_PROFILE=web-only
```

`Makefile` 先构建模块、`build/bin/live_stream`、`build/bin/live_sysupgrade` 和
`www/dist`，再执行：

```sh
scripts/package_release.sh $(RELEASE_DIR) $(RELEASE_VERSION) $(RELEASE_PROFILE)
```

`scripts/package_release.sh` 的参数和环境变量：

- 参数 1 `release_dir`：输出目录，默认 `release`，不能是仓库根目录。
- 参数 2 `version`：写入镜像内 `version` 文件和 `Install.Version`，只允许
  字母、数字、`.`、`_`、`+`、`-`。
- 参数 3 `profile`：升级包类型，默认 `web-only`。
- `UPGRADE_SIGN_KEY`：必填，离线私钥路径，用于签名 `Install`。
- `UPGRADE_PUBLIC_KEY`：可选，默认 `configs/upgrade_public_key.pem`，脚本用它
  立即验签，设备端也需要把同一公钥部署到 `/config/upgrade_public_key.pem`。
- `MKSQUASHFS`、`MKFS_JFFS2`：可选，用于指定宿主机打镜像工具；不指定时优先使用
  `tools/pc/` 下的工具，再回退到 `PATH`。

脚本支持的 profile：

| profile | 产物 | `Install.Commands` | 是否要求重启 | 说明 |
| --- | --- | --- | --- | --- |
| `web-only` | `web.squashfs` | `web` | 否 | 主进程可在线卸载 `/www`、写 `/dev/mtd4`、重新挂载 |
| `bin-web` | `bin.squashfs`、`web.squashfs` | `bin`、`web` | 是 | 涉及 `/opt/app`，必须走 RAM helper |
| `config-only` | `config.jffs2` | `config` | 是 | 写 `/config` 后重启生效 |
| `kernel-rootfs` / `full` | 无 | 无 | 无 | 当前脚本直接拒绝，原因是 rootfs 在线升级不安全 |

打包过程按固定顺序执行：

1. 清理并创建 `release/bin`、`release/configs`、`release/web`、`release/flash`。
2. 复制 `build/bin/live_stream`、`build/bin/live_sysupgrade`、`configs/*.json`、
   可选公钥和 `www/dist`。
3. 按 profile 生成镜像：
   - `bin.squashfs`：从 `flash/bin_root` 生成，包含 `bin/live_stream`、
     `sbin/live_sysupgrade` 和 `version`。
   - `web.squashfs`：从 `flash/web_root` 生成，包含 Web 静态资源和 `version`。
   - `config.jffs2`：从 `flash/config_root` 生成，包含配置 JSON 和
     `upgrade_public_key.pem`。
4. 对每个 payload 计算 sha256，写入 `flash/Install` 的 `Commands`。
5. 用 `UPGRADE_SIGN_KEY` 对 `Install` 原文做 SHA256/RSA 签名，生成
   `flash/Install.sig`。
6. 立刻用 `UPGRADE_PUBLIC_KEY` 验证 `Install.sig`，验不过则发布失败。
7. 用 `zip -0` 生成 store-only 包 `flash/upgrade-<profile>.zip`，并复制一份为
   `flash/upgrade.zip`。

最终给 Web 上传的是 `release/flash/upgrade.zip` 或
`release/flash/upgrade-<profile>.zip`。升级 zip 里只放 `Install`、
`Install.sig` 和 manifest 声明的镜像文件，不把要执行的 helper 当作 zip entry
直接运行。`live_sysupgrade` 是受信任的板端程序：当前系统从
`/opt/app/sbin/live_sysupgrade` 复制到 tmpfs 后执行；新包中的 `bin.squashfs`
只为升级完成后的下一版系统携带新的 helper。

## 升级包格式

升级包是 store-only zip，必须包含：

```text
Install
Install.sig
<payload image files>
```

`Install.sig` 是对 `Install` 原文做 SHA256/RSA 签名。设备只保存公钥，默认路径为
`/config/upgrade_public_key.pem`；源码内置公钥未替换时必须 fail-closed。私钥只属于
离线打包环境，不能进入设备、升级包或仓库。

`Install` 使用 JSON：

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
      "Partition": "web",
      "File": "web.squashfs",
      "Sha256": "..."
    }
  ]
}
```

只接受 `Action=burn`，`Board=Hi3516DV300`，`Flash=spi-nor-32m`，
`PackageType=normal`。当前允许分区是 `kernel`、`bin`、`web`、`config`；
`boot`、`rootfs` 和 `data` 必须拒绝。每个 payload 必须被 manifest 声明，文件名必须是
安全相对路径，sha256、大小和镜像魔数必须匹配目标分区。升级包中存在未声明文件、
压缩 entry、重复 entry、绝对路径、反斜杠路径或 `..` 路径都必须拒绝。

镜像魔数约束：

- `kernel`：uImage magic `0x27051956`。
- `bin` / `web`：squashfs `hsqs`。
- `config`：jffs2 magic `0x8519`。

## 端到端升级通路

升级通路分为“离线发布”和“板端执行”两段。脚本只负责在 PC/CI 上生成带签名的
`upgrade.zip`；板端不运行打包脚本，只消费 zip 包里的 `Install`、`Install.sig` 和镜像。

```mermaid
flowchart TD
  MakeRelease[make release] --> PackageScript[scripts/package_release.sh]
  PackageScript --> Zip[release/flash/upgrade.zip]
  Zip --> Upload[POST /api/upgrade/upload]
  Upload --> PackageParser[system upgrade_package]
  PackageParser --> Start[POST /api/upgrade/start]
  Start --> StateMachine[IUpgrade 状态机]
  StateMachine --> Platform[app/platform/linux UpgradePlatform]
  Platform -->|web-only| WebWrite[主进程写 /dev/mtd4]
  Platform -->|bin/config/kernel| RamHelper[/tmp/live_stream/upgrade/live_sysupgrade]
  RamHelper --> FlashWriter[upgrade_flash 写 MTD]
  WebWrite --> Status[/data/upgrade_status.json]
  FlashWriter --> Status
  WebWrite --> Log[/data/upgrade.log]
  FlashWriter --> Log
```

HTTP 入口由 `libs/http/src/handlers/upgrade_handler.cpp` 提供：

| API | 权限 | 作用 |
| --- | --- | --- |
| `POST /api/upgrade/upload?filename=<name>` | `kUpgrade` | 接收 zip body，保存并立即校验 |
| `GET /api/upgrade/status` | `kReadStatus` | 返回内存中的升级状态 |
| `POST /api/upgrade/validate` | `kUpgrade` | 对已上传包重新校验 |
| `POST /api/upgrade/start` | `kUpgrade` | 启动异步升级任务 |
| `POST /api/upgrade/cancel` | `kUpgrade` | 请求取消尚可取消的升级 |
| `POST /api/upgrade/confirm-reboot` | `kUpgrade` | 用户确认重启 |

上传入口只接受最大 32M 的 body。文件名只允许字母、数字、`.`、`-`、`_`，拒绝
`/`、`\` 和 `..`，最终文件保存到
`/tmp/live_stream/upgrade/uploads/<timestamp>-<name>`。写文件使用
`O_EXCL|O_NOFOLLOW`，写完 `fsync`；校验失败会删除上传文件。后续
`validate/start` 只接受该 upload 目录下的真实 regular file：`system` 会先
`lstat` 拒绝符号链接，再 `realpath` 校验路径前缀、大小和文件类型。

包校验由 `libs/system/src/upgrade_package.cpp` 完成，顺序是：

1. 读取 zip local file header，只接受 store-only entry，拒绝压缩、data descriptor、
   重复 entry、绝对路径、反斜杠路径和 `..` 路径。
2. 找到 `Install` 和 `Install.sig`。
3. 在解析 JSON 前，先用 `/config/upgrade_public_key.pem` 对 `Install` 原文做
   SHA256/RSA 验签；公钥未部署或仍是占位内容时直接失败。
4. 解析并校验 `Install`：目标板卡、flash 类型、包类型、动作、分区白名单、sha256
   格式、文件路径和重复命令。
5. 对每个 manifest 声明的 payload 校验存在、大小不超过目标分区、sha256 匹配。
6. 拒绝所有未被 manifest 声明的 payload entry。

`POST /api/upgrade/start` 的请求体来自 Web，字段为：

```json
{
  "package_path": "/tmp/live_stream/upgrade/uploads/1710000000000-upgrade.zip",
  "expected_version": "1.2.3",
  "allow_same_version": false,
  "allow_downgrade": false,
  "auto_reboot": true
}
```

`IUpgrade` 只管理状态机、权限后的审计、版本策略和异步执行，不直接碰 MTD。升级任务
投递到单 worker executor，保证同一时间只有一个升级在跑。状态和进度映射如下：

| 状态 | 进度 | 含义 |
| --- | ---: | --- |
| `validating` | 0 | 重新校验包、读取版本和目标信息 |
| `preparing` | 10 | 创建 stage、确认 tmpfs/MTD 布局 |
| `writing` | 20-89 | 解包和写 flash |
| `committing` | 90 | 写入完成，提交状态 |
| `waiting_reboot` | 100 | 需要用户或 helper 重启 |
| `completed` | 100 | 已完成 |
| `failed` | 100 | 失败，错误写入状态 |
| `canceled` | 当前进度 | 用户取消 |

版本策略在写 flash 前执行：`expected_version` 不为空时必须匹配包版本；同版本必须显式
`allow_same_version=true`；降级必须显式 `allow_downgrade=true`。取消只在
`validating`、`preparing`、`writing` 三个阶段接受；真正进入 MTD erase/write 后，
取消只能阻止后续可中断步骤，不能把已经擦写的分区恢复回来。

`app/platform/linux/upgrade_platform.cpp` 是板端实际执行层。它把升级分成两条路径：

- `web-only`：包里只有 `web` 命令，主进程可以在线完成。
- 系统升级：包含 `bin`、`config` 或 `kernel` 时，必须复制并启动 RAM helper。

`web-only` 包由主进程在线处理：

1. `PrepareUpgrade` 在 `/data/upgrade/staged/<timestamp-version>` 创建 staging 目录。
2. `WriteUpgrade` 把 payload 解到 staging，解包进度映射到写入阶段前半段。
3. `ApplyWebUpgrade` 卸载 `/www`。
4. 调用 `upgrade_flash::WriteMtdImage` 写 `/dev/mtd4`。
5. 写完后重新把 `/dev/mtdblock4` 以 squashfs 只读方式挂载到 `/www`。
6. `CommitUpgrade` 写 `/data/upgrade_status.json` 和 `/data/upgrade.log`。

非 `web-only` 包走 RAM helper，原因是 `bin` 分区承载当前正在运行的
`/opt/app/bin/live_stream` 和 `/opt/app/sbin/live_sysupgrade`，`config` 也可能被当前进程
读取；擦写这些分区前必须让主业务进程退出，并且执行升级的程序不能继续从即将卸载或擦写的
`/opt/app` 取指令。流程是：

1. `PrepareUpgrade` 确认 `/tmp/live_stream/upgrade` 的实际挂载类型是 `tmpfs` 或
   `ramfs`。
2. 校验 `/proc/mtd` 和 `MEMGETINFO` 返回的分区名、大小、erase size 与内置分区表一致。
3. 从当前系统的 `/opt/app/sbin/live_sysupgrade` 复制到
   `/tmp/live_stream/upgrade/live_sysupgrade`；源文件和目标文件都拒绝符号链接，目标用
   `O_EXCL|O_NOFOLLOW` 新建并 `chmod 0755`。
4. `fork/exec` tmpfs 中的 helper：

   ```sh
   /tmp/live_stream/upgrade/live_sysupgrade \
     --package /tmp/live_stream/upgrade/uploads/<file>.zip \
     --stage /tmp/live_stream/upgrade/staged \
     --reboot
   ```

5. 主进程把内存状态推进到 `waiting_reboot`；后续真实写入、日志和重启由 helper 接管。

`app/tools/sysupgrade/live_sysupgrade.cpp` 启动后不信任主进程已经做过的校验，会重新执行
完整校验：

1. 重新解析参数和升级包，重新验签 `Install`，重新校验 payload。
2. 再次拒绝 `rootfs` 在线升级。
3. 确认 staging 目录位于 tmpfs/ramfs。
4. 再次校验 MTD 布局。
5. 解包到 `/tmp/live_stream/upgrade/staged`，把解包进度写到
   `/data/upgrade_status.json`。
6. 如果包内存在非 `web` 分区，执行 `/etc/init.d/S80live_stream stop`、
   `killall live_stream`、`pkill live_stream` 停止主业务。
7. 按 `Install.Commands` 顺序卸载对应挂载点并写 MTD。
8. 写 `/data/upgrade.log`、`/data/upgrade_status.json`，`sync` 后按参数或 manifest
   要求重启。

MTD 写入流程固定为：

```text
open /dev/mtdX
ioctl MEMGETINFO
校验 /proc/mtd 名称、设备、大小、erase size
打开镜像并校验 regular file、大小、sha256、magic
MEMERASE 全分区
write 镜像
fsync
readback sha256
close
```

写入阶段失败必须停止后续分区。失败原因写 `/data/upgrade.log`，升级状态写
`/data/upgrade_status.json`。当前 32M NOR 方案不是 A/B 升级；断电或写坏系统分区时，
恢复手段是 UART/U-Boot/TFTP 或烧录器。

## 发布与联调命令

只发布 Web 静态资源：

```sh
UPGRADE_SIGN_KEY=/secure/upgrade_private_key.pem \
  make release RELEASE_VERSION=1.2.3 RELEASE_PROFILE=web-only
```

发布主程序和 Web：

```sh
UPGRADE_SIGN_KEY=/secure/upgrade_private_key.pem \
UPGRADE_PUBLIC_KEY=configs/upgrade_public_key.pem \
  make release RELEASE_VERSION=1.2.3 RELEASE_PROFILE=bin-web
```

只发布配置分区：

```sh
UPGRADE_SIGN_KEY=/secure/upgrade_private_key.pem \
  make release RELEASE_VERSION=1.2.3-config RELEASE_PROFILE=config-only
```

脚本级回归入口是：

```sh
scripts/tests/package_release_test.sh
```

该测试关注打包脚本的 profile、签名、拒绝策略和 zip 结构；板端写 MTD 仍必须在目标板或
等价 MTD 环境验证。

## 升级验证

必须覆盖以下验证项：

- `make release` 在缺少 `UPGRADE_SIGN_KEY`、缺少公钥、无打镜像工具、非法 version
  时失败。
- `web-only`、`bin-web`、`config-only` 三类包的 `Install.Commands`、`Reboot`、
  payload sha256 和 zip store-only 格式正确。
- `kernel-rootfs` 和 `full` profile 必须被脚本拒绝。
- `/proc/mtd` 分区名、大小和 erase size 与内置分区表一致。
- `/opt/app`、`/www`、`/config`、`/data` 挂载点存在，`/tmp/live_stream/upgrade`
  位于 RAM 文件系统。
- 单独升级 `web`。
- 单独升级 `bin`。
- 单独升级 `config` 并重启后配置可用。
- 组合升级 `bin + web`、`kernel + bin + web + config`。
- 包含 `boot`、`rootfs`、`data` 的普通包必须拒绝。
- 缺失 `Install.sig`、签名错误、公钥未部署必须拒绝。
- 未声明文件、sha256 错误、文件超过分区、镜像魔数错误必须拒绝。
- `/proc/mtd` 或 `MEMGETINFO` 布局不匹配必须拒绝。
- helper 源路径或目标路径遇到符号链接必须拒绝。
- 升级 `config` 后 `/data/upgrade.log` 和 `/data/upgrade_status.json` 不丢失。

## 非目标

- 不直接写 MTD 或绕过 `IUpgradePlatform`。
- 不拥有网络接口管理、DNS 配置或协议监听生命周期。
- 不在 Web 侧推导系统状态。

## 风险与优化方向

- 系统动作必须做权限和审计。
- 查询路径应保持轻量，避免频繁读取阻塞文件影响 Web 状态刷新。
- 时间跳变会影响日志、认证过期和媒体时间戳展示，需要记录关键变更。
- 写 flash 前必须校验签名、manifest、分区白名单和包完整性。
- `rootfs` 在线升级、A/B 回滚、断电恢复和 U-Boot 自动回滚目前都不是本模块能力；
  如果产品要求升级内核/rootfs 后可自动恢复，需要新增独立 recovery 设计，而不是放开
  当前普通 Web 升级路径。
