# Upgrade Runtime And SPI NOR Design

## 模块定位

升级运行设计覆盖 32M SPI NOR 分区、工厂烧写、Linux 挂载、发布包布局、
`live_sysupgrade` 和 MTD 写入边界。HTTP 上传和升级状态归
`../libs/upgrade-service-design.md`，打包脚本归
`../operations/release-package-design.md`。

## 总体框架图

```mermaid
flowchart LR
  Web[Web Console] --> HTTP[http_service upgrade handlers]
  HTTP --> UpgradeSvc[upgrade_service]
  UpgradeSvc --> Platform[UpgradePlatform]
  Platform --> Package[upgrade package verify/extract]
  Package --> Sysupgrade[live_sysupgrade]
  Sysupgrade --> MTD[MTD partitions]
  MTD --> Boot[boot/kernel/rootfs/bin/web/config/data]
```

## SPI NOR 分区

32M SPI NOR 使用固定分区：

| 分区 | 起始地址 | 大小 | 挂载点 | 说明 |
| --- | ---: | ---: | --- | --- |
| `boot` | `0x00000000` | 1M | 无 | U-Boot + env，普通升级禁止写 |
| `kernel` | `0x00100000` | 4M | 无 | `uImage_hi3516dv300` |
| `rootfs` | `0x00500000` | 12M | `/` | jffs2 rootfs |
| `bin` | `0x01100000` | 10M | `/opt/app` | 主程序、动态库、脚本 |
| `web` | `0x01B00000` | 2M | `/www` | Web 静态资源 |
| `config` | `0x01D00000` | 1M | `/config` | 用户配置 |
| `data` | `0x01E00000` | 2M | `/data` | 日志、升级状态、运行数据 |

`mtdparts`：

```text
mtdparts=hi_sfc:1M(boot),4M(kernel),12M(rootfs),10M(bin),2M(web),1M(config),2M(data)
```

普通 Web 升级禁止擦写 `boot`，避免破坏 U-Boot env。

## 文件系统规划

- `rootfs`：busybox、基础系统库、MTD 支持、启动脚本和挂载点。
- `bin.squashfs`：`bin/live_stream`、`sbin/live_sysupgrade`、动态库和 app 脚本。
- `web.squashfs`：`www/dist` 静态资源。
- `config.jffs2`：业务配置、默认配置、认证用户配置和升级公钥。
- `data.jffs2`：操作日志、升级状态和运行数据。
- `/tmp/live_stream/upgrade` 必须位于 RAM，用于上传后的临时解包和校验。

## 升级流程

1. Web 通过 HTTP 上传升级包。
2. `upgrade_service` 校验状态和平台依赖，调用平台层处理包。
3. `UpgradePlatform` 校验签名、manifest、分区目标和包内容。
4. `live_sysupgrade` 在受控状态下写允许升级的 MTD 分区。
5. 升级状态写入 `/data`，需要重启时由 Web 确认。

## 风险控制

- `boot` 默认不可升级。
- `config` 可单独或联合升级，但字段语义必须保持向后兼容。
- 写 MTD 前必须完成包完整性校验和目标分区校验。
- 上传、校验、写入和重启确认都要有明确状态，避免 Web 展示猜测进度。
