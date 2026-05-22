# Embedded Quality Tooling

本文档记录当前 `live_stream` 嵌入式产品工程的代码质量、性能和设计扫描工具。
目标是快速判断“现在有什么、缺什么、先装什么、怎么扫”，不替代具体问题的
专项 review。

## Scope

默认扫描生产代码和工程配置：

- `app/`
- `libs/`
- `configs/`
- `www/`
- 根目录 `Makefile`、`.clang-format` 和模块 `Makefile`

默认跳过：

- `3rdparty/`，除非问题指向第三方集成或链接。
- `build/`、`out/`、`www/dist/` 等产物目录。
- `tests/`，除非任务明确要求测试迁移、修复或覆盖率分析。

## Current Baseline

当前工程已经具备：

- 交叉编译工具链：`arm-himix200-linux-gcc`、`arm-himix200-linux-g++`、
  `arm-himix200-linux-strip`、`arm-himix200-linux-size`。
- 构建入口：根目录 `make -j2`，服务模块 `make -C libs/<service>`。
- 格式配置：根目录 `.clang-format`，基于 Google 风格，但本工程缩进配置为
  4 空格，且不自动排序 include。
- C++ 编译约束：C++17、`-Wall`、`-Wextra`、服务模块 `-Werror`、
  `-fno-exceptions`、`-fno-rtti`。
- 前端构建入口：`www/package.json` 中的 `npm run build`、`npm run dev`。

当前环境已检测到的可用工具：

| Category | Available |
| --- | --- |
| Search/version/build | `rg`、`git`、`make` |
| Cross toolchain | `arm-himix200-linux-gcc`、`arm-himix200-linux-g++`、`arm-himix200-linux-strip`、`arm-himix200-linux-size` |
| Host compiler | `gcc`、`g++` |
| Static analysis/format | `clang-tidy`、`clang-format`、`cppcheck` |
| Static analyzer | `scan-build-10` |
| Compilation database | `bear` |
| Runtime diagnosis | `strace`、`ltrace`、`gdb` |
| Binary inspection | `readelf`、`objdump`、`nm`、`size`、`addr2line`、`file` |
| Frontend | `node`、`npm`、`npx` |

当前环境未检测到的常用工具：

| Category | Missing |
| --- | --- |
| LLVM compiler/tooling | `clang`、`clang++`、unversioned `scan-build` |
| Include dependency | `include-what-you-use`、`iwyu` |
| Code size statistics | `cloc`、`tokei` |
| Runtime profiling/checking | `perf`、`valgrind` |
| Cross debug | `gdb-multiarch` |
| Frontend direct commands | global `tsc`、global `eslint`、`playwright` |

说明：`www` 的 TypeScript 和 Vite 是项目依赖，优先使用 `npm run build` 或
`npx tsc`，不要求全局安装 `tsc`。

## Recommended Install Set

优先安装这些，收益最高：

```sh
sudo apt install clang clang-tools clang-tidy clang-format cppcheck bear \
  cloc linux-perf gdb-multiarch valgrind
```

如果发行版包名不同，按系统实际包名调整：

- `linux-perf` 可能叫 `linux-tools-$(uname -r)`、`perf` 或拆在内核工具包中。
- `clang-tools` 通常提供 `scan-build`。
- `include-what-you-use` 可能需要单独安装，且版本最好和 clang 主版本接近。

可选安装：

```sh
sudo apt install include-what-you-use tokei
```

前端可选质量工具建议作为项目依赖引入，而不是全局安装：

```sh
cd www
npm install -D eslint typescript playwright
```

是否加入 ESLint、Playwright 应单独决定；不要只为扫描工程而扩大前端工具链。

## Tool Roles

`rg` 用于第一轮高效扫描。重点查高风险调用、临时注释、全局状态、线程、锁、
阻塞调用、内存拷贝和高频日志。

`make -j2` 用于确认当前交叉编译基线。嵌入式项目先能构建，再谈静态分析结论。

`bear` 用于生成 `compile_commands.json`，给 `clang-tidy`、clangd 和部分分析工具
提供真实编译参数。

`clang-tidy` 用于 C++ 设计和缺陷扫描。优先启用 `bugprone-*`、`performance-*`、
`readability-*`、`modernize-*` 中与本工程约束不冲突的检查。注意本工程不使用
exceptions 和 RTTI，不能照搬桌面 C++ 建议。

`scan-build-10` 已随 `clang-tools-10` 可用，可作为 `clang-tidy` 外的补充静态
分析工具。当前环境没有无后缀的 `scan-build` 命令，命令示例应使用
`scan-build-10 make -j2`。它会用宿主 clang analyzer 包装编译命令，遇到交叉
编译专用参数时可能失败；脚本会把这类结果记录为 warning，不阻断整次扫描。

`cppcheck` 用于补充扫描空指针、越界、未初始化、资源泄漏和可移植性问题。它对
Makefile 型嵌入式工程比较实用，但误报需要人工分级。

`clang-format` 用于格式检查。当前工程已有 `.clang-format`，但配置和文档中的
2 空格约定不完全一致；不要未经确认全量格式化。

`cloc` 或 `tokei` 用于代码量和模块规模统计，帮助定位过大的服务、文件和函数。

`readelf`、`objdump`、`nm`、`size`、`addr2line` 用于产物体积、符号、链接依赖和
崩溃地址定位。嵌入式产品应保留带符号调试产物，再对发布产物 strip。

`strace` 用于查运行期系统调用、路径、阻塞、网络和文件访问问题。适合 HTTP、
配置、升级、日志和进程启动问题。

`ltrace` 用于查动态库调用。若最终产物大量静态链接，收益会低于 `strace`。

`gdb` 和 `gdb-multiarch` 用于 crash、死锁和目标板远程调试。目标板通常配合
`gdbserver`。

`perf` 用于 CPU 热点采样。是否可用取决于目标内核配置、权限和符号保留情况。

`valgrind` 用于内存错误检查。HiSilicon ARM 目标板未必可直接使用；可在宿主侧
对可移植模块做补充验证，不能替代目标板实测。

## First-Pass Commands

推荐使用脚本统一收集结果：

```sh
scripts/quality_scan.sh
scripts/quality_scan.sh full
```

默认 `quick` 模式会运行构建、前端构建、关键词扫描、`cppcheck`、代码规模统计
和产物信息检查。`full` 模式会额外运行 `bear`、`scan-build-10` 和
`clang-tidy`。最终总报告写入 `docs/quality/quality_report.md`；原始工具输出写入
`reports/quality/<timestamp>/`，只作为排查证据。缺失的可选工具会记录为 skipped；
`scan-build-10` 的交叉编译兼容性问题会记录为 warning，不会直接导致扫描失败。

`docs/quality/quality_report.md` 会从原始日志中提取：

- 必须先看的失败步骤和对应日志。
- 必须优先处理的 `cppcheck` error。
- 需要人工 review 的 `cppcheck` warning 和 `clang-tidy` 诊断。
- 命中风险关键词最多的文件。
- 热路径、发送、编码、日志相关候选优化点。
- 构建失败尾部摘要。

注意：关键词和热路径命中只是候选优化点，不等同于 bug。真正的修复优先级应先看
`Must Check First` 和 `Must Fix`。

快速基线：

```sh
make -j2
cd www && npm run build
```

生成编译数据库：

```sh
bear make -j2
```

补充静态分析：

```sh
scan-build-10 make -j2
```

生产代码关键词快扫：

```sh
rg -n "TODO|FIXME|XXX|HACK|sleep|usleep|malloc|free|new |delete |memcpy|strcpy|sprintf|printf|pthread|mutex|lock|detach" app libs configs www
```

高频日志和热路径扫描：

```sh
rg -n "LOG|Log|printf|std::cout|PublishFrame|OnFrame|Encode|Write|Send|Push" app libs
```

模块规模统计：

```sh
cloc app libs configs www --exclude-dir=dist,node_modules
```

静态分析示例：

```sh
cppcheck --enable=warning,performance,portability --std=c++17 \
  --suppress=missingIncludeSystem app libs
```

```sh
clang-tidy app/*.cpp libs/*/src/*.cpp -- -std=c++17 -Iapp -Ilibs/infra_service/include
```

实际使用 `clang-tidy` 时优先基于 `compile_commands.json`，不要长期维护一份手写
include 参数。

产物检查：

```sh
file build/bin/live_stream
arm-himix200-linux-size build/bin/live_stream
readelf -h build/bin/live_stream
nm -S --size-sort build/bin/live_stream
```

## Embedded Review Checklist

正确性：

- 服务启动、停止、失败回滚是否顺序清楚。
- 动作型函数是否用 `bool` 表示成败，失败路径是否可追踪。
- 取值型函数失败时是否返回约定的空值或默认失败值。
- public header 是否自包含，include guard 是否符合项目格式。
- 配置 JSON 字段和 HTTP API DTO 是否保持兼容。

性能：

- 视频帧路径是否避免不必要的分配、拷贝和日志。
- HLS/FLV/WebRTC 分发是否有明确缓存边界和丢帧策略。
- 抓图、编码、封装、网络发送是否存在阻塞主链路的调用。
- 常驻日志是否只覆盖启动、停止、配置变化、错误和关键状态变化。
- 二进制体积和静态链接库是否符合目标板 flash/内存预算。

设计：

- `media_service`、`stream_hub_service`、`http_service`、`webrtc_service`
  是否遵守模块边界。
- 上层是否消费真实状态，而不是重复推导设备内部状态。
- 是否存在只转调、只包装条件、只隐藏 2-3 行逻辑的 helper/class。
- 命名是否说明业务事实，避免离开局部上下文就不清楚的 `data`、`target`、
  `previous_state` 等泛名。
- 是否引入音频、录像、存储回放等当前产品范围外能力。

可维护性：

- 是否有过大的 `.cpp` 或服务模块，需要拆分时是否按真实职责拆。
- 是否存在公共 API 频繁变化但文档、前端 DTO 未同步。
- 是否存在全局单例、隐式所有权或生命周期注释缺失。
- 是否存在跨模块直接 include 对方 `src/` 内部头文件的情况。

## Suggested Workflow

1. 先跑 `make -j2` 和 `npm run build`，确认基线。
2. 用 `rg` 做关键词快扫，记录高风险文件。
3. 用 `cloc` 或 `tokei` 看模块规模，把大文件和高 churn 文件放前面。
4. 用 `bear make -j2` 生成 `compile_commands.json`。
5. 对高风险模块跑 `clang-tidy` 和 `cppcheck`，先人工分级，不直接大面积改。
6. 对目标产物跑 `size`、`readelf`、`nm`，建立体积和符号基线。
7. 上板后用 `strace`、`gdbserver`、`perf` 做运行期验证。
8. 报告按“必须修、建议修、暂不动”分级，避免把风格偏好当成缺陷。

日常扫描可直接使用：

```sh
scripts/quality_scan.sh quick
```

专项 review 或阶段性质量盘点再使用：

```sh
scripts/quality_scan.sh full
```

## Gaps To Close

短期建议补齐：

- 安装 `cloc` 或 `tokei`，用于规模和模块统计。
- 安装 `gdb-multiarch`，配合目标板 `gdbserver` 做远程调试。
- 安装 `perf`，为视频热路径建立 CPU 采样手段。
- 安装 `valgrind`，用于宿主侧可移植模块内存检查。
- 如需要统一命令名，再安装 `clang-tools` 元包或增加 `scan-build=scan-build-10`
  的本地别名。

中期再考虑：

- 安装并引入 `include-what-you-use`，做头文件依赖治理；当前环境未安装。
- 给 `clang-tidy` 增加项目配置，明确启用检查和禁用项，避免和
  `-fno-exceptions`、`-fno-rtti`、嵌入式交叉编译约束冲突。
- 给前端补 ESLint/Playwright，但前提是明确希望长期维护这些质量门禁；当前
  `www` 只有 Vite/TypeScript 构建链路。
- 建立目标板 profile 流程，记录符号保留、采样命令和报告位置；这需要目标板、
  内核 perf 支持和发布/调试产物约定。
