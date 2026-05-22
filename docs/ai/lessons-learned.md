# live_stream 项目经验总结

## 项目本质

这个项目不是普通 Web 项目，而是一个嵌入式视频实时预览系统。

核心链路：

```text
HiSilicon MPP / VENC
  -> media_service
  -> stream_hub_service
  -> http_service: HLS / FLV / snapshot / config API
  -> webrtc_service
  -> www React Console
```

关键难点：

- 视频链路状态多：启动、停止、重启、首帧、关键帧、编码格式、码流 ready。
- 前后端状态必须一致：前端不能猜流是否 ready。
- 嵌入式环境限制多：交叉编译、无异常、无 RTTI、日志不能乱打、实时路径不能重。
- 模块之间有强时序：配置切换、编码重启、HLS/FLV/WebRTC 可用状态都有依赖。
- 代码需要可运行，不只是结构好看。

## 已有优点

1. 模块方向基本正确

   `media_service`、`stream_hub_service`、`http_service`、`webrtc_service`、`www` 这个拆法合理。采集/编码、浏览器流封装、HTTP API、WebRTC、前端管理台没有完全混在一起。

2. 构建体系相对清楚

   后端 `libs/*_service` 复用 `libs/service_rules.mk`，前端独立在 `www/`，整体能用 `make -j2` 验证。

3. 状态字段已有基础

   例如 `hlsReady`、`flvReady`、`webrtcReady`、`browserCodec` 这些字段方向是对的。前端只要消费这些状态，就能少很多猜测逻辑。

4. 功能边界比较聚焦

   项目目标明确：实时预览、抓图、配置、运维管理。不做音频、不做录像、不做存储回放，这个范围控制是优点。

5. 可以渐进式清理

   很多问题可以通过删封装、改命名、收日志、收紧接口语义解决，不需要推倒重来。

## 主要问题

1. 顶层规划不足

   一开始没有稳定的模块职责图、状态流图、接口清单。后续改代码时容易从局部文件出发，导致重复封装、重复判断、重复造接口。

2. 接口设计不够先行

   有些逻辑是先写实现，再补接口语义。正确顺序应该是先确定谁拥有状态、谁暴露状态、调用方怎么消费，再写实现。

3. AI 中断后上下文恢复差

   中断后如果只看当前文件，不重新看最近提交、已有接口、模块边界，就容易出现重新编码、老接口不用、又封装一套。

4. 重构目标不够单一

   有些轮次同时做了命名、删日志、拆函数、改流程，导致风险变大，也让改动显得来回反复。

5. 过度设计倾向明显

   一些地方用了 `Context`、`State`、`Manager`、`Store`、小 helper，但实际业务只是几行顺序逻辑。结果不是更清楚，而是更难读。

6. 命名没有统一审查

   `Target`、`Context`、`State`、`was_started`、`previous` 这类名字泛、虚、不贴业务。能编译，但维护者理解成本高。

7. 日志策略不成熟

   帧路径、首帧路径、诊断日志容易越加越多。嵌入式实时系统里日志必须低频、明确、有退出机制。

8. 前后端契约没有文档化

   前端依赖哪些状态字段、字段语义是什么、ready 表示什么，没有单独文档固化。结果前端容易自己猜。

## 如果从 0 开始的推荐流程

### 第一阶段：架构文档先行

不写代码，先产出 4 份文档：

- `docs/architecture/overview.md`

  写模块职责、主数据流、线程/生命周期边界。

- `docs/stream-state.md`

  写 stream 状态机：running、pending、stopped、hlsReady、flvReady、webrtcReady、关键帧请求时机。

- `docs/api-contract.md`

  写 HTTP API 和前端消费字段，尤其是配置 API、状态 API、HLS/FLV/WebRTC API。

- `docs/development-rules.md` 或写进 `AGENTS.md`

  写命名规则、禁止过度封装、日志规则、提交粒度、验证方式。

### 第二阶段：接口先定

先设计 public interface：

- `IMediaService`：负责视频配置、启动停止、MPP/VENC 生命周期。
- `IStreamHubService`：负责编码帧分发、HLS/FLV 浏览器流状态、关键帧请求。
- `IWebrtcService`：负责 WebRTC peer 生命周期。
- `http_service`：只做 API 适配，不拥有视频状态。
- `www/api`：只封装 API，不写设备业务判断。
- `www/hooks`：消费后端状态，不自己发明 ready 逻辑。

### 第三阶段：后端主链路

先跑通：

```text
VENC -> stream_hub -> HLS/FLV -> HTTP
```

WebRTC 和复杂配置先不要急。先保证主码流、子码流、H264、首帧、关键帧、ready 状态稳定。

### 第四阶段：配置管理

视频配置、图像配置、码流切换要明确：

- 哪些配置需要重启 pipeline。
- 哪些配置可以热更新。
- 失败后怎么处理。
- 不要过度 rollback，除非设备状态真的可恢复且有明确语义。

### 第五阶段：前端管理台

前端按职责拆：

- `pages/`：页面组织。
- `components/`：纯 UI。
- `hooks/`：生命周期和数据拉取。
- `api/`：HTTP 封装。
- `types.ts`：契约类型。

播放器逻辑可以放 `usePreviewPlayer`，但不要再拆成十几个小 hook。

### 第六阶段：WebRTC

等 HLS/FLV 稳定后再加 WebRTC。WebRTC 状态复杂，应该作为独立链路接入，不要和 HLS/FLV ready 逻辑互相污染。

### 第七阶段：收尾

最后统一做：

- 日志降噪。
- 命名清理。
- 大文件职责检查。
- 构建验证。
- 文档补齐。

## 需要的 agents

如果使用 AI 协作，建议拆成这些 agent。每个 agent 必须有明确边界，不能到处乱改。

1. Architecture Agent

   职责：维护架构图、模块职责、接口边界。

   不写大量业务代码，只审设计是否跑偏。

2. Backend Pipeline Agent

   职责：`media_service`、HiSilicon MPP、VENC、配置生命周期。

   重点：设备状态、启动停止、编码参数、失败处理。

3. Stream Hub Agent

   职责：`stream_hub_service`、帧分发、HLS/FLV 封装状态、关键帧请求。

   重点：实时路径不能乱加日志和锁。

4. HTTP API Agent

   职责：`http_service`、API 契约、状态字段输出。

   重点：不拥有业务状态，只转译模块状态。

5. WebRTC Agent

   职责：peer 生命周期、SDP、ICE、后端接口。

   重点：不要影响 HLS/FLV 主链路。

6. Frontend Agent

   职责：React UI、hooks、API 消费。

   重点：前端不猜设备状态，只消费后端字段。

7. Cleanup/Review Agent

   职责：专门扫命名、过度封装、日志噪音、重复接口。

   这个 agent 不应该主动大改，只列问题和做小范围清理。

8. Build/Verification Agent

   职责：构建、交叉编译、前端 build、回归检查。

   重点：每轮改动后给明确验证结果。

## 需要的 skills

1. `google-c-cpp-style`

   已经存在，必须继续用。

   内容重点：C++ 命名、include、无异常、无 RTTI、2 空格、`bool` 返回约定。

2. `embedded-video-pipeline`

   建议新增。

   内容应包括：

   - MPP/VENC 生命周期规则。
   - 编码帧处理规则。
   - 主/子码流状态定义。
   - 关键帧请求策略。
   - 实时路径禁止事项。
   - 日志限制。

3. `live-stream-architecture`

   建议新增。

   内容应包括：

   - 模块职责图。
   - 哪个模块拥有哪个状态。
   - 禁止跨层访问。
   - 新增接口前必须查已有接口。
   - API 契约变更流程。

4. `frontend-ipc-console`

   建议新增。

   内容应包括：

   - React 目录规则。
   - API/hooks/components 职责。
   - 前端不解析 SDK 配置。
   - 播放器 ready 逻辑必须来自后端。
   - UI 风格：IPC/NVR 管理台，不做营销页。

5. `code-cleanup-discipline`

   建议新增。

   内容应包括：

   - 优先删代码。
   - 不为简单顺序逻辑抽 helper。
   - 禁止泛名滥用：`Context`、`Manager`、`Target`、`State`。
   - 日志低频原则。
   - 每轮只做一种改动。

## 需要的文档

1. `AGENTS.md`

   继续作为最高频规则。

   应包含：

   - 项目结构。
   - 构建命令。
   - C++ 规则。
   - 前端规则。
   - 不做的功能范围。
   - 变更原则。
   - 提交规则。
   - AI 协作规则。

2. `docs/architecture/overview.md`

   应包含：

   - 模块图。
   - 进程/线程模型。
   - 数据流。
   - 控制流。
   - 配置流。
   - 错误处理原则。

3. `docs/module-boundaries.md`

   应包含：

   - `media_service` 负责什么。
   - `stream_hub_service` 负责什么。
   - `http_service` 负责什么。
   - `webrtc_service` 负责什么。
   - `www` 负责什么。
   - 明确禁止跨层逻辑。

4. `docs/api-contract.md`

   应包含：

   - `/api/stream/status` 字段语义。
   - `hlsReady/flvReady/webrtcReady` 准确定义。
   - 配置 API schema。
   - WebRTC API。
   - 兼容性要求。

5. `docs/stream-state.md`

   应包含：

   - stream 生命周期。
   - 浏览器流 ready 条件。
   - HLS ready 条件。
   - FLV ready 条件。
   - WebRTC ready 条件。
   - 请求关键帧策略。

6. `docs/logging.md`

   应包含：

   - 哪些日志允许。
   - 哪些路径禁止高频日志。
   - 错误日志、状态变化日志、诊断日志区别。
   - 临时诊断日志必须有删除计划。

7. `docs/refactor-rules.md`

   应包含：

   - rename、cleanup、bugfix、refactor 分开。
   - 不混合提交。
   - 新增抽象检查清单。
   - 新旧接口复用检查清单。

## 每轮开发固定流程

1. 恢复上下文：

   ```sh
   git status --short
   git log --oneline -5
   ```

2. 阅读相关接口，不直接写代码。

3. 明确本轮类型：

   ```text
   bugfix / cleanup / rename / refactor / feature
   ```

4. 明确不做什么。

5. 小范围修改。

6. 构建验证：

   ```sh
   make -j2
   npm run build
   ```

   按实际改动选择，不强行跑无关项。

7. 提交，一次提交只做一件事。

## 嵌入式工程师如何高效使用 Codex

Codex 更适合作为快速代码阅读器、重复劳动执行器、局部实现助手、review 工具、构建验证助手和文档整理助手，不适合放任它自由决定顶层架构。

嵌入式项目里，工程师应先确定这些事情：

- 模块边界。
- 状态归属。
- 硬件时序。
- 资源生命周期。
- 失败策略。
- 实时路径限制。

Codex 再按这些边界执行实现、重构、验证和文档整理。

### 容易漏掉的教训

1. AI 容易局部最优

   它看到一个函数烂，就会优化这个函数；但嵌入式系统真正的问题常在链路：状态源、时序、锁、缓冲、错误恢复、资源生命周期。

2. 不要让 AI 自己发明架构

   谁拥有流状态、谁负责关键帧、谁能重启 pipeline，这些顶层边界必须由工程师先定。

3. 嵌入式代码要优先可预测

   设备初始化、SDK 调用、资源释放、失败处理，最好是一眼能看出顺序和退出路径。优雅抽象不如直线流程可靠。

4. 日志不是越多越安全

   帧路径日志会拖慢系统、淹没真正问题。更好的方式是状态变化日志、错误日志和可开关诊断。

5. ready 状态必须有权威来源

   前端、HTTP 层、业务层都不应该猜设备状态。谁最接近真实资源，谁提供状态，其他层只消费。

6. AI 倾向补一层抽象

   比如 helper、manager、context、store、adapter。新增这些之前必须先说明为什么不能直接写，为什么已有接口不能用。

7. 中断是高风险点

   一旦上下文断了，AI 可能重新造轮子。每次恢复必须先读 `git log`、`git status`、接口文件和最近 diff。

8. AI 不天然理解硬件代价

   它可能写出看起来没问题但在嵌入式上很差的代码：频繁分配、频繁日志、锁范围大、轮询太密、失败重试无上限、状态抖动。

9. 不要把结构性问题切成无穷小补丁

   小修适合局部 bug，不适合接口混乱、状态源分散、热路径多次解析、模块边界错误这类问题。遇到结构性问题时，
   先看当前文件实际内容、`git status --short` 和相关 diff，保护用户已有改动；然后评估是否值得架构级重构。
   如果一次大改能显著减少代码量、提升性能或让职责边界更清楚，就应优先大改并同步删掉旧路径，而不是不断加适配层。

### 给 Codex 的任务模板

推荐用短而明确的任务描述：

```text
目标：
范围：
禁止：
优先复用：
验证：
提交：
```

示例：

```text
目标：修复 FLV 首次切换偶现卡住。
范围：www/src/hooks/usePreviewPlayer.ts，必要时只读 stream status API。
禁止：不新增状态机，不新增定时重试，不改 API。
优先复用：hlsReady/flvReady/webrtcReady。
验证：npm run build。
提交：单独 commit。
```

这种任务比“优化一下”更省时间，也更不容易产生偏离目标的代码。

### 推荐协作节奏

1. 先让 Codex 扫描，不改代码。

   ```text
   只分析，不改代码。列出模块边界、已有接口、状态流、可能风险。
   ```

2. 再让 Codex 给方案。

   ```text
   给 2-3 个方案，说明改动范围、风险、验证方式。不要写代码。
   ```

3. 工程师选方案后再实现。

   ```text
   按方案 1 实现。先判断这是局部 bug 还是结构性问题；如果架构级改动能明显减少复杂度/提升性能，就一次性收敛同类问题，不做适配式小补丁。
   ```

4. 实现后让 Codex 自审。

   ```text
   review 你刚才的改动，重点检查重复封装、命名、锁范围、
   日志噪音、接口复用、行为变化。
   ```

5. 最后验证和提交。

   ```text
   运行对应构建。只提交本次相关文件。
   ```

## Token 消耗与效率平衡

减少 token 消耗的重点不是让 AI 少说几句，而是减少返工、重复扫描和重复编码。

真正烧 token 的地方通常是：

- 中断后重新读一遍工程。
- 没有边界，反复扫描全项目。
- 写错方向后大段 diff、再 review、再回滚。
- 新增抽象后又删掉。
- 旧接口没查到，重复封装一套。
- 每次都让 AI 从 0 理解模块。

### 平衡原则

1. 高频规则放 `AGENTS.md`

   项目结构、构建命令、命名规则、禁止事项、提交规则放在高频文档里，减少每轮重复说明。

2. 复杂经验放低频文档

   类似本文这种复盘文档，不需要每轮都读。需要做架构整理、阶段性总结、协作规则调整时再读。

3. 文档要短，不要百科全书

   文档太长会增加每轮读取成本。更好的方式是：

   - `AGENTS.md`：高频硬规则。
   - `docs/architecture/overview.md`：一页模块图。
   - `docs/api-contract.md`：接口字段语义。
   - `docs/refactor-rules.md`：重构禁忌。

4. 任务范围越窄，token 越省

   ```text
   只检查 www/src/hooks/usePreviewPlayer.ts 的 FLV ready 逻辑，不扫后端。
   ```

   比“看看整个工程哪里有问题”省很多。

5. 先分析再动手，通常更省

   看起来多一步，其实能避免写错方向。跨模块问题尤其要先确认状态源和已有接口。

6. 用索引文档替代反复全工程搜索

   建议维护 `docs/module-index.md`：

   ```text
   media_service: 视频配置/MPP/VENC 生命周期
   stream_hub_service: 帧分发/HLS/FLV ready
   http_service: API 转译
   www/api: HTTP 封装
   www/hooks: 前端生命周期
   ```

   Codex 先读索引，再定向读文件，不需要每次全工程扫。

7. 全工程 review 要阶段性使用

   日常 bugfix 只查相关链路；rename 只查引用；cleanup 只查目标模块；架构 review 才扫全工程。

### Token 使用模式

低 token 模式：

```text
只读这 2 个文件，找最小修复，不写长分析。
```

适合小 bug、小命名、小 UI 调整。

中 token 模式：

```text
读相关模块和接口，给方案后实现。
```

适合跨 2-3 个模块的功能。

高 token 模式：

```text
全工程扫描，输出架构/技术债/重构计划，不改代码。
```

适合阶段性整理，不适合每天使用。

最省 token 的方式不是让 AI 少读，而是让它只读该读的东西，并且一次做对方向。

## 最重要的经验

这个项目后续想快，不是靠 AI 多写，而是靠 AI 少乱写。

必须做到：

- 先有架构图，再写代码。
- 先查已有接口，再新增接口。
- 先确定状态拥有者，再消费状态。
- 先删无用代码，再考虑抽象。
- 每轮只解决一个目标。
- 中断后必须恢复上下文。
- 文档和 skill 必须记录已经踩过的坑。

如果从 0 开始按这套做，整体速度会快很多，返工会少很多，代码也不会变成“局部能跑、整体发散”的状态。
