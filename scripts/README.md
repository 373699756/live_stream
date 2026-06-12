# Scripts README

本文档说明工程脚本的常用入口。当前最重要的是质量扫描脚本
`quality_scan.py`，用于构建、契约、静态分析、前端检查、热路径候选和质量基线门禁。

## Quality Scan

先进入工程根目录：

```sh
cd /home/cp/Public/hisi/live_stream
```

### 日常开发

只看当前变更相关的扫描项：

```sh
python3 scripts/quality_scan.py quick --scope changed
```

合入前使用历史基线，只阻断新增问题：

```sh
python3 scripts/quality_scan.py quick --scope changed --baseline scripts/quality_baseline.json
```

### 格式化规则

项目统一使用 4 个空格缩进，不使用 tab：

- C/C++ 由根目录 `.clang-format` 控制，要求 `IndentWidth: 4`、`TabWidth: 4`、`UseTab: Never`。
- Web 由 `www/.prettierrc.json` 控制，要求 `tabWidth: 4`、`useTabs: false`。

Web 源码格式化使用：

```sh
cd www
npm run format
```

质量扫描会生成 `format-config.log`，如果上述配置被改掉，扫描会直接失败。

### 全工程扫描

大节点或夜间跑全工程 quick 扫描：

```sh
python3 scripts/quality_scan.py quick --scope all --baseline scripts/quality_baseline.json
```

需要更重的静态分析时使用 full 模式，额外执行 scan-build、clang-tidy 和
include-what-you-use：

```sh
python3 scripts/quality_scan.py full --scope all --baseline scripts/quality_baseline.json
```

### 基线生成与刷新

从某次 findings 生成或刷新基线：

```sh
python3 scripts/quality_scan.py baseline --from-findings scripts/reports/quality/quality_findings.json --output scripts/quality_baseline.json
```

刷新前必须确认 `--from-findings` 指向期望的扫描结果。全量基线应来自
`--scope all` 的扫描结果，不要误用 changed 扫描结果覆盖全量基线。不要为了绕过
门禁刷新基线；只有确认问题属于历史债或已单独记录时才更新。

## Reports

每次扫描都会生成独立报告目录：

```text
scripts/reports/quality/<时间戳>/
```

重点文件：

```text
scripts/reports/quality/quality_report.md           # 最终需要修复问题报告，每次扫描覆盖
scripts/reports/quality/<时间戳>/findings.json       # 本次机器可读 findings
scripts/reports/quality/quality_findings.json       # 最新 findings，每次扫描覆盖
scripts/reports/quality/<时间戳>/findings.sarif      # 本次 SARIF
scripts/reports/quality/quality_findings.sarif      # 最新 SARIF，每次扫描覆盖
scripts/reports/quality/<时间戳>/baseline-diff.json  # 使用 --baseline 时生成
```

原始工具日志也在同一目录：

```text
make.log
clang-format.log
cppcheck.log
lizard.log
flawfinder.log
semgrep.log
www-build.log
www-typecheck.log
```

日常只看 `quality_report.md`，只有需要追证据时才看各工具日志。

## Exit Code

```text
0 = 没有阻断项
1 = 有失败步骤，或 baseline 发现新增阻断问题
```

启用 `--baseline` 后，已进入 `scripts/quality_baseline.json` 的历史诊断不会单独阻断；
新增 `warning` 或 `error` 默认阻断，新增 `note` 只进入报告。
