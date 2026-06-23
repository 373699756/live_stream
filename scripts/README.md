# Scripts README

本文档只说明仓库脚本入口和生成物边界。脚本按职责保留独立入口，不合成
`tool.sh` 这类总控脚本。

## Packaging

`make debug` 调用：

```sh
scripts/package_debug.sh debug
```

该脚本只生成可直接运行的调试目录，内容包括 `bin/`、`configs/`、`models/`、
`web/` 和 `log/`。

`make release` 调用：

```sh
scripts/package_release.sh release 1.2.3 all
```

该脚本负责正式升级包：拷贝 release 输入、剥离符号、生成 `bin.squashfs`、
`web.squashfs`、`config.jffs2`、写入 `Install` manifest、签名并输出
`upgrade-<profile>.zip`。支持的 profile 是 `all`、`web-only`、`bin-web` 和
`config-only`。

release 打包测试入口：

```sh
scripts/tests/package_release_test.sh
```

`package_debug.sh` 和 `package_release.sh` 不合并成一个脚本：debug 是运行目录
staging，release 是升级镜像和签名流程，职责不同。

## Quality

日常开发只看当前变更：

```sh
python3 scripts/scan/quality_scan.py quick --scope changed
```

合入前用历史基线，只阻断新增问题：

```sh
python3 scripts/scan/quality_scan.py quick --scope changed --baseline scripts/scan/quality_baseline.json
```

大节点或夜间跑全工程 quick 扫描：

```sh
python3 scripts/scan/quality_scan.py quick --scope all --baseline scripts/scan/quality_baseline.json
```

需要更重的静态分析时使用 full 模式，额外执行 scan-build、clang-tidy 和
include-what-you-use：

```sh
python3 scripts/scan/quality_scan.py full --scope all --baseline scripts/scan/quality_baseline.json
```

从某次 findings 刷新基线：

```sh
python3 scripts/scan/quality_scan.py baseline --from-findings scripts/scan/reports/quality/quality_findings.json --output scripts/scan/quality_baseline.json
```

刷新前必须确认 `--from-findings` 指向期望扫描结果。全量基线应来自
`--scope all` 的扫描结果，不要用 changed 扫描结果覆盖全量基线。

质量扫描相关文件：

- `scripts/scan/quality_scan.py`：质量扫描编排入口。
- `scripts/scan/quality_semgrep.yml`：semgrep 规则。
- `scripts/scan/quality_baseline.json`：历史问题基线。
- `scripts/scan/check_http_web_contract.py`：Web API 与后端 HTTP route 契约检查。
- `scripts/scan/check_cpp_style_contract.py`：C++ 缩进和风格配置契约检查。

两个 contract 脚本被 `make host-test` 和 `quality_scan.py` 直接调用，不并入
`quality_scan.py`。

## Board Probe

板端热路径采集入口：

```sh
scripts/board_hot_path_probe.sh --base-url http://127.0.0.1:8080 --stream main --duration 120 --interval 2
```

它用于在板端运行 HLS/FLV/MJPEG/WebRTC 预览负载时采集 CPU、RSS、媒体会话、
pending bytes、丢帧和接口响应。常见输出：

```text
metrics.csv
raw/*.json
clients.log
run.env
```

该脚本依赖目标板运行态和可选客户端命令，不属于 host 质量扫描，也不并入
`quality_scan.py`。

## Rootfs Templates

`scripts/rootfs/etc/init.d/S20mount_app` 和
`scripts/rootfs/etc/init.d/S80live_stream` 是 rootfs/init 模板，不是临时脚本。
它们用于板端挂载 `/opt/app`、`/www`、`/config`、`/data` 并启动
`live_stream`。

## Generated Outputs

以下内容是生成物，不应提交：

- `scripts/scan/reports/quality/`：质量扫描报告和原始工具日志。
- `scripts/__pycache__/`、`*.pyc`：Python 字节码缓存。

`scripts/scan/reports/quality/quality_report.md` 是日常查看入口；需要追证据时再看同目录
原始工具日志。

## Exit Code

质量扫描退出码：

```text
0 = 没有阻断项
1 = 有失败步骤，或 baseline 发现新增阻断问题
```

启用 `--baseline` 后，已进入 `scripts/scan/quality_baseline.json` 的历史诊断不会单独阻断；
新增 `warning` 或 `error` 默认阻断，新增 `note` 只进入报告。
