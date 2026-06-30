# live_stream

HiSilicon IPC video live-preview service.

`live_stream` 是面向海思 IPC 开发板的视频实时预览、设备配置和运维管理程序。项目范围固定为 video-only，核心目标是稳定预览链路、清晰的设备配置、可诊断的运行状态和可控的板端资源占用。

## 产品范围

支持：

- 主/子码流实时预览。
- HLS、HTTP-FLV、MJPEG、RTSP、WebRTC 视频链路。
- 抓图、OSD、隐私遮挡、ROI、图像参数配置。
- AI 告警、周界区域、告警抓拍和基础事件联动。
- Web Console 运维管理、升级、日志和状态查看。

不支持：

- 音频采集、音频编码、音频传输和相关 UI/API。
- 录像、录制、存储回放、MP4、RTMP、推流、代理、GB28181。
- TURN/SFU、云信令、设备云绑定、WebRTC datachannel。
- 通用媒体服务器控制台。

## 目录结构

- `app/`：进程入口和组合根，负责装配 foundation、device、media、protocol 等 subsystem。
- `libs/<module>/`：后端模块，每个模块通常包含 `include/`、`src/`、`Makefile` 和 `module.mk`。
- `configs/`：开发和样例 JSON 配置。
- `www/`：Vite + React + TypeScript + plain CSS 的 IPC Web Console。
- `docs/`：模块契约、重构计划、Web 设计和热路径资源模型。
- `scripts/`：打包、扫描、发布和辅助脚本。
- `3rdparty/`：第三方依赖区域，除非任务明确要求不要改动。

## 构建

在仓库根目录构建默认调试产物：

```sh
make -j2
```

调试打包产物是可直接拷贝到板端运行目录的结构，当前不再复制开发配置文件到 debug 目录：

```text
debug/
  bin/
  log/
  models/
  web/
```

板端常见运行方式是在部署目录下启动，例如：

```sh
cd /mnt
./bin/live_stream
```

运行时资源路径保持简单明确：

- Web 静态资源：相对运行目录的 `web/`。
- 日志目录：相对运行目录的 `log/`。
- release 运行配置：默认从 `/config/*.json` 读取。
- AI 模型：优先使用配置路径；相对路径会按可执行文件目录和其父目录解析，例如 `/mnt/models/*.wk`。

发布升级包：

```sh
UPGRADE_SIGN_KEY=/path/to/private_key.pem \
  make release RELEASE_VERSION=1.2.3
```

release 文件写入 `release/`。默认 `RELEASE_PROFILE=all`，生成 `bin.squashfs`、`web.squashfs`、`config.jffs2` 和签名升级 zip。仅发布 Web Console 时使用 `RELEASE_PROFILE=web-only`。

前端构建：

```sh
cd www
npm run build
```

## 工程原则

- 状态归属最接近真实资源的模块，其他模块只消费明确接口。
- 热路径优先控制内存、拷贝、队列上限、关闭路径和日志量。
- API、配置、Web 类型、mock 和文档必须同批更新。
- 不为旧接口、旧命名或旧路径新增长期兼容层。
- 重构任务按主题拆分，bugfix、rename、cleanup、架构拆分不要混在一起。
- 板端问题优先做可诊断状态和可回退设计，不用临时延时掩盖生命周期问题。

## 文档入口

- [AGENTS.md](AGENTS.md)：AI 协作、编码约定和项目执行规则。
- [docs/README.md](docs/README.md)：文档总索引。
- [docs/refactor/README.md](docs/refactor/README.md)：当前唯一重构计划。
- `docs/libs/<module>.md`：模块 public API、配置、事件、状态来源和资源边界。
- [docs/web/web-console-design.md](docs/web/web-console-design.md)：Web Console 设计和 API 消费规则。

AI 辅助修改代码时先读 [AGENTS.md](AGENTS.md)，再读 [docs/README.md](docs/README.md) 和目标模块文档。
