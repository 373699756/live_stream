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

`boot` 包含 U-Boot 和 env，普通 Web 升级永远不得擦写。`rootfs` 当前挂载为 `/`，
没有 recovery 分区、initramfs 或 A/B 回滚能力，Linux 在线升级必须拒绝。`data`
保存运行日志、操作日志和升级状态，不纳入普通升级。

`bin.squashfs` 承载 `/opt/app/bin/live_stream`、`/opt/app/sbin/live_sysupgrade`、
业务动态库和脚本；`web.squashfs` 承载 Web 静态资源；`config.jffs2` 承载运行配置、
认证用户配置和 `/config/upgrade_public_key.pem`。

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

## 升级运行流程

HTTP 上传入口保存到 `/tmp/live_stream/upgrade/uploads`。上传和 validate 阶段只校验包，
不得擦写 flash。

`web-only` 包由主进程在线处理：

1. 解包到 `/data/upgrade/staged/<timestamp-version>`。
2. 卸载 `/www`。
3. 使用 MTD ioctl 写 `/dev/mtd4`。
4. 重新挂载 `/www`。

非 `web-only` 包走 RAM helper：

1. 准备阶段确认 `/tmp/live_stream/upgrade` 的实际挂载类型是 `tmpfs` 或 `ramfs`。
2. 校验 `/proc/mtd` 和 `MEMGETINFO` 返回的分区名、大小、erase size。
3. 从设备内置 `/opt/app/sbin/live_sysupgrade` 复制到
   `/tmp/live_stream/upgrade/live_sysupgrade`。
4. helper 复制必须拒绝符号链接，目标文件用 `O_EXCL|O_NOFOLLOW` 新建。
5. fork/exec `/tmp` 中的 helper。
6. helper 重新验签、重新校验 payload、重新校验 MTD 布局。
7. helper 停止 `live_stream`，按分区卸载 `/opt/app`、`/www` 或 `/config`，写 flash，
   `sync` 后重启。

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

## 升级验证

必须覆盖以下验证项：

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
