# live_stream Codex 项目说明

## 项目结构

- `app/` 是 C++ 程序入口和组合根，当前主入口为 `app/application/main.cpp`。
- `libs/<module>/` 是模块目录，通常包含 `include/`、`src/`、
  `Makefile` 和 `module.mk`。
- `libs/module_rules.mk` 定义模块的通用构建规则。
- `configs/` 存放运行配置 JSON，例如业务配置和认证用户配置。
- `www/` 是 IPC Web Console，技术栈为 Vite、React、TypeScript 和
  plain CSS/CSS variables。
- `3rdparty/` 是第三方依赖区域，`build/` 是构建产物区域。除非任务明确要求，
  不要改动这些目录中的内容。

## AI 入口与上下文策略

- 每次任务先读本文件。
- 普通编码任务再读 `docs/README.md`，再按任务范围定向读取相关
  `docs/libs/<module>.md`、`docs/web/web-console-design.md`，或
  `docs/refactor/README.md` 的热路径资源章节。
- `README.md` 是项目定位、构建和运行入口；`docs/README.md` 是文档索引；
  `docs/refactor/README.md` 是重构计划；长期设计不再按 active、architecture、
  contracts、features、quality 这类横切目录存放，API、配置、事件、AI、
  升级、质量工具等内容都归入拥有它们的模块文档。
- 不要在普通实现任务里扩写长设计文档；状态、接口、契约或架构决策更新到
  对应模块文档，索引变化同步更新 `docs/README.md`。
- 为控制 token，日常任务默认只读相关模块和相邻接口；全工程扫描只用于明确的
  架构 review、技术债盘点或用户要求。

## 常用命令

在仓库根目录：

```sh
make -j2
make clean
make host-test
make board-test-build
```

构建单个模块：

```sh
make -C libs/<module>
```

测试目标语义：

- `make host-test` / `make test` 只运行宿主可执行的质量检查，例如 HTTP/Web
  契约、C++ 风格契约和前端构建。
- `make board-test-build` / `make test-build` 交叉编译各模块测试二进制。
- `make board-test` 会执行交叉编译测试二进制，只应在目标板或兼容运行环境使用。

前端开发和构建在 `www/` 目录下执行：

```sh
npm run dev
npm run build
```

## C++ 约定

- 采用交叉编译器：arm-himix200-linux-，项目使用 C++17，构建开启
  `-Wall -Wextra -Werror`。
- 不使用 C++ exceptions 和 RTTI；动作型函数返回 `bool` 表示成败，
  取值型函数返回原本业务类型，失败时返回空字符串、空容器、0、空对象或
  `nullptr` 等该类型的默认失败值。
- 保持现有 Google-like 风格：
  - 缩进使用 4 个空格，和 `.clang-format` 保持一致。
  - 类型名使用 `PascalCase`。
  - 函数名使用 `PascalCase`。
  - 变量和参数使用 `snake_case`。
  - 常量使用 `kName` 风格。
  - class 成员变量使用尾随下划线，例如 `logger_`。
- 新增 public header 时保持自包含，并使用
  `LIVE_STREAM_<MODULE>_<FILE>_H_` 形式的 include guard。
- include 顺序遵循现有代码习惯：相关项目头、C/C++ 标准库、其他项目头。
- 避免引入宏技巧、隐式所有权或过度抽象。所有权、生命周期、线程假设不明显时，
  需要在接口或实现附近说明。

## 模块约定

- 新增模块时遵循现有模式：`libs/<module>/Makefile`、`module.mk`、
  `include/` 和 `src/`。
- 模块构建规则应复用 `libs/module_rules.mk`，不要复制一套新的通用规则。
- 产品范围固定为视频实时预览、抓图、配置和运维管理；不实现音频采集、音频编码、
  音频传输，也不实现录像、录制、存储回放或相关 UI/API。
- 测试内容默认不修改；除非用户明确要求修复、新增或迁移测试，不主动编辑
  `tests/` 目录或测试用例内容。
- 当前阶段优先完成生产代码、架构边界和可运行功能；测试模块暂不主动整理或迁移，
  等整个项目功能完成后再统一处理，除非任务明确要求。
- 不随意修改 public API、配置 JSON schema 或 HTTP API 契约，除非任务明确要求。
- 配置文件中的字段语义应保持向后兼容；确需变更时，要同步更新调用方和文档。
- 重构方向是逐步删除 `*Dependencies` DTO：基础服务通过 `Runtime` 安装和查询，
  直播源通过 `MediaSourceRegistry` 查询，协议只读状态通过 `ServiceRegistry` 查询。
  `Runtime` 只放 logger/config/auth/event/socket_io 这类基础服务，registry 不允许暴露
  跨模块业务控制能力。

## 前端约定

- `www/` 使用 React + TypeScript + plain CSS/CSS variables。
- UI 保持 IPC/NVR 管理台风格：密集表单、紧凑左侧导航、清晰状态和深色预览区域。
- 不做营销页式布局，不添加与设备管理无关的装饰性大图或介绍页。
- 后端 API 契约参考 `www/README.md`，前端不直接拥有设备 SDK 配置解析逻辑。
- 当后端不可用时，保留现有 mock 数据路径以便 UI 开发。

## 变更原则

- 优先保持现有目录结构、命名和构建方式。
- 改动应聚焦当前任务，避免无关重构、格式化或构建产物变更。
- 不删除或重命名现有空 `.codex` 文件，除非用户明确要求。
- 涉及运行时路径时，先确认资源类型和构建模式：debug 目录不再复制开发配置文件；
  release 配置默认使用 `/config/*.json`；Web、log、models 等运行资源通常按当前部署目录
  或可执行文件目录解析。

## 设计与命名原则

- 精简优先。先写直接、可读、可验证的代码；只有在重复、复杂度或边界问题真实出现时才抽象。
- 低耦合、高内聚。模块只暴露必要接口；状态、生命周期和资源所有权应留在最了解它们的模块内。
- 状态来源必须单一。版本、升级状态、AI 能力、媒体 ready、设备生命周期这类信息只能有一个
  权威来源；不要为了兜底添加多套推导、多文件查找或前端二次判断。
- 板端问题优先修生命周期和状态边界，不用简单延时掩盖竞态。涉及 MPP、VPSS、VENC、AI 抓帧、
  抓图、升级刷写时，先确认谁拥有资源、谁能在重建期间访问资源。
- 热路径优先控制内存、拷贝、队列上限、关闭路径和日志量。HLS/FLV/MJPEG/WebRTC/RTSP
  的帧路径不能新增普通诊断日志、无界缓存或大对象频繁复制。
- 避免重复封装。不要新增只转调一两个函数、只包装一个条件、只改名传参的 helper。
- 新增代码必须按功能职责拆分文件；配置/校验、运行态、协议处理、缓存/队列、
  硬件生命周期等不同职责不要堆在同一个大文件里。内部文件名要直指业务事实，
  避免 `helpers`、`utils`、`manager` 这类泛名；只有很小且只服务当前实现的局部函数
  才留在当前 `.cpp` 的匿名 namespace。
- 抽象必须有收益。新增类、结构体、枚举或设计模式前，必须能说明它减少了重复、隔离了变化、
  收拢了状态，或明确了跨模块契约；否则用普通函数和顺序流程。
- 设计模式按需选择。不要为了套模式引入 Manager、Context、Tracker、State、Result、Target
  等泛名对象；这些名字只有在业务语义清楚且职责单一时才允许使用。
- 命名必须直指业务事实。变量名应说明“是什么/为什么存在”，例如
  `stream_was_running`、`system_was_initialized`、`need_main_key_frame`；
  避免 `was_started`、`previous_state`、`target`、`data` 这类离开局部上下文就看不懂的名字。
- 有副作用的流程按步骤写清楚。不要用 `ok = a && b`、`applied = cond || Call()` 压缩生命周期、
  配置、IO、硬件调用等流程；失败路径应直接、可追踪、易打断。
- 日志克制。常驻日志只保留启动、停止、配置变化、错误和关键状态变化；不要默认加入
  “前 N 帧 diag”“只打一遍日志”这类调试开关，排障临时日志在任务完成后删除。
- 做 review 或清理时先扫全工程生产代码，再把问题分为“可直接删/改”“命名差但有业务必要”
  “高风险需单独确认”。不要把正常领域概念误报为坏味道，例如 HTTP handler、网络 event handler、
  审计 `RequestContext`、配置回滚这类确有业务语义的代码。

## 协作与执行原则

- 写代码前先明确假设、目标和验证方式；存在多种解释时先说明取舍，必要时询问。
- 非平凡任务先写清楚：目标、范围、不做什么、主模块、相邻接口模块、公共契约变化、
  配置/API/Web/mock/doc 是否同步、热路径或资源风险、验证方式、回滚方式。
- 先判断问题类型：局部 bug 用直接修复；命名、接口、状态源、热路径、模块边界这类结构性问题，
  如果架构级改动能明显降低复杂度、减少代码量或提升性能，就优先一次性收敛同类问题，不做反复适配的小补丁。
- 先冻结契约，再改实现。公共 C++ API、HTTP API、配置字段、事件 payload、Web DTO
  不允许边写边漂移；确需变更时同步后端、前端、mock、配置样例和拥有模块文档。
- 不为了“兼容旧写法”堆适配层。用户明确要求去兼容、收敛接口、清理设计时，应同步改调用方、
  删除旧路径和旧命名，避免新旧两套逻辑长期并存。
- 严禁新增“兼容适应”代码或过渡文件来维持新旧两套路径；旧接口、旧命名或旧文件不适合继续维护时，
  应完整重构到目标设计，同步更新调用方并删除旧路径，不用 wrapper、alias、bridge、legacy adapter
  之类文件掩盖边界问题。
- 修改前先看当前文件实际内容、`git status --short` 和相关 diff，识别用户已有改动；执行时不覆盖用户改动，
  但也不要因为怕动面而回避必要的大改。动面大时先给清晰计划，再按模块分批提交。
- 每个改动都应能追溯到用户请求；多步任务应先给出简短计划，并围绕可验证目标推进。
- 验证应和风险匹配；文档或说明类改动可以只检查 diff，不强行运行无关构建或测试。

## 重构执行经验

- 一轮任务只碰一个主模块，最多带一个相邻接口模块；不要把 bugfix、rename、cleanup、
  refactor 混在一起。
- 不新增新的 `*Dependencies` DTO 或变相依赖包。迁移旧依赖时按 `Runtime`、
  `MediaSourceRegistry`、`ServiceRegistry` 三条路径收敛，不用 Context、Bundle、
  Sources 之类名字重新包装一遍。
- HTTP/Web API 表达产品能力，不暴露内部模块结构。URL 生成、协议状态、错误 envelope
  由后端统一提供，Web 不拼设备 SDK 或协议内部细节。
- 配置保存成功必须表示配置已通过拥有模块 verify/apply；不能只表示 JSON 写入成功。
- AI 是可选能力。AI 后端、模型、抓帧或告警失败只更新 AI 状态，不影响直播、抓图和系统启动。
- 升级刷写期间 HTTP 状态查询不能依赖被杀掉的主进程；涉及升级状态时优先保持状态服务和刷写
  执行路径同生命周期设计。
- Web 提示语要区分“正在处理、成功、失败、可重试、需要刷新”。旧错误在用户修改输入、
  切换目标或重新提交时必须清理，不能跨操作残留。
- 文档分层：`README.md` 写项目定位和运行入口，`AGENTS.md` 写协作与编码纪律，
  `docs/refactor/README.md` 写重构计划，`docs/libs/<module>.md` 写模块契约。

## Git 提交

- 使用 Conventional Commits 格式。
- 提交信息描述“为什么”。
- 每次完成关键改动后立即进行一次 git 提交；不要把多个无关关键改动堆到同一个提交里。
