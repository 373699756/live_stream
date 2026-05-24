# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

复核 32M SPI NOR 升级模块全流程，明确升级签名公私钥放置规则，并补充升级相关
测试代码。

## Problem fixed

- RAM helper 复制到 `/tmp/live_stream/upgrade/live_sysupgrade` 时补充软链接防护：
  源文件使用 `O_NOFOLLOW` 并要求普通文件，目标先删除旧路径后用
  `O_EXCL|O_NOFOLLOW` 新建，权限通过 fd 设置。
- 文档补齐私钥、公钥、打包端校验、公钥轮换和 fail-closed 行为。
- 打包脚本测试覆盖 `web-only`、`bin-web`、`config-only`、签名验证、公钥保留，
  以及 `kernel-rootfs/full` 拒绝。
- `upgrade_service` 测试补充上传目录 symlink 拒绝、auto reboot、reboot 失败后的
  committed recovery 提示。
- 新增 HTTP 升级路由测试源码，覆盖 upload/validate/start/status/cancel/reboot
  的权限和参数传递。

## Files changed

- `.gitignore`
- `app/upgrade_platform.cpp`
- `docs/active/spi_nor_32m_upgrade_plan.md`
- `docs/active/coder_report.md`
- `libs/http_service/tests/http_upgrade_handler_test.cpp`
- `libs/upgrade_service/tests/upgrade_service_behavior_test.cpp`
- `scripts/tests/package_upgrade_test.sh`

## Verification

通过：

- `sh -n scripts/package_upgrade.sh`
- `sh -n scripts/tests/package_upgrade_test.sh`
- `scripts/tests/package_upgrade_test.sh`
- `git diff --check`
- `g++ ... -c libs/http_service/tests/http_upgrade_handler_test.cpp`
- `make -C libs/upgrade_service`
- `make -C libs/http_service ../../build/tests/http_service_http_upgrade_handler_test`

受限：

- `make -C libs/upgrade_service test` 已编译出 ARM ELF 测试程序，但开发机直接执行
  报 `Exec format error`；需要板端或 qemu-arm 执行。
- 新增 `http_upgrade_handler_test` 已交叉编译通过，但同样是 ARM ELF，开发机不能
  直接运行。
- `make -C libs/http_service test` 当前被旧
  `libs/http_service/tests/http_service_header_test.cpp` 只 include 前置声明导致的
  incomplete type 编译错误阻塞，非本次升级改动引入。

## Commit

Pending.

## Deviations

没有提交或生成生产私钥；真实 `configs/upgrade_public_key.pem` 仍需生产发布流程提供。

## Blocked or follow-up

MTD 实刷、断电恢复、`/tmp` tmpfs、`/proc/mtd` layout mismatch 等必须在板端或 mock
MTD 环境继续验证。
