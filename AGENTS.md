# live_stream Codex 项目说明

## 项目结构

- `app/` 是 C++ 程序入口，当前主入口为 `app/runtime/main.cpp`。
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
  `docs/libs/<module>.md`、`docs/web/web-console-design.md` 或
  `docs/optimization/memory.md`。
- `docs/README.md` 是文档索引。长期设计不再按 active、architecture、
  contracts、features、quality 这类横切目录存放；API、配置、事件、AI、
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
- `*Dependencies` struct 只作为组合根构造注入 DTO，不作为实现类长期保存的依赖包；
  实现类应解包为语义明确的非 owning 成员指针。业务 service、net、media、auth
  不做全局单例或 ServiceLocator。

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
- 涉及运行时路径时，注意当前代码使用相对路径，例如 `configs/*.json` 和
  `log/operation.log`。

## 设计与命名原则

- 精简优先。先写直接、可读、可验证的代码；只有在重复、复杂度或边界问题真实出现时才抽象。
- 低耦合、高内聚。模块只暴露必要接口；状态、生命周期和资源所有权应留在最了解它们的模块内。
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
- 先判断问题类型：局部 bug 用直接修复；命名、接口、状态源、热路径、模块边界这类结构性问题，
  如果架构级改动能明显降低复杂度、减少代码量或提升性能，就优先一次性收敛同类问题，不做反复适配的小补丁。
- 不为了“兼容旧写法”堆适配层。用户明确要求去兼容、收敛接口、清理设计时，应同步改调用方、
  删除旧路径和旧命名，避免新旧两套逻辑长期并存。
- 严禁新增“兼容适应”代码或过渡文件来维持新旧两套路径；旧接口、旧命名或旧文件不适合继续维护时，
  应完整重构到目标设计，同步更新调用方并删除旧路径，不用 wrapper、alias、bridge、legacy adapter
  之类文件掩盖边界问题。
- 修改前先看当前文件实际内容、`git status --short` 和相关 diff，识别用户已有改动；执行时不覆盖用户改动，
  但也不要因为怕动面而回避必要的大改。动面大时先给清晰计划，再按模块分批提交。
- 每个改动都应能追溯到用户请求；多步任务应先给出简短计划，并围绕可验证目标推进。
- 验证应和风险匹配；文档或说明类改动可以只检查 diff，不强行运行无关构建或测试。

## Git 提交

- 使用 Conventional Commits 格式。
- 提交信息描述“为什么”。
- 每次完成关键改动后立即进行一次 git 提交；不要把多个无关关键改动堆到同一个提交里。
