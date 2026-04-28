# 单进程多线程 IPC 软件设计文档

## 1. 目标

本系统面向海思嵌入式 Linux IPC 设备，采用单进程、多线程架构。主程序为 `live_stream`，内部按独立静态库拆分 service。系统提供 Web 管理、WebRTC 拉流、RTSP 拉流、ONVIF、抓图、配置管理、鉴权、时间同步、网络配置、OSD、升级、运行日志、用户操作审计和系统健康监控能力。

核心目标：

- 媒体采集和编码只做一份，多协议共享同一路编码输出。
- `media_service` 是唯一直接访问海思 MPP/SDK 的模块。
- RTSP、WebRTC、ONVIF、抓图、录像只依赖 `media_service` 的 public interface。
- 配置修改统一经过 `config_service`，其他模块不能直接读写配置文件。
- 普通运行日志由 `infra_service` 提供基础 `Log` 能力。
- 用户操作审计独立为 `logger_service`，不混入普通运行日志接口。
- 各 service 编译为独立静态库，既能由顶层 Makefile 统一编译，也能在各自目录单独编译。
- 先完成可编译、可启动、可测试的骨架，再逐步接入真实业务。

默认技术约束：

- 语言：C++17。
- 构建：Makefile。
- 链接：静态库。
- 编码规范：Google C++ Style Guide。
- 默认不使用 C++ exception 和 RTTI。

## 2. 模块清单

主程序：

- `live_stream`

核心静态库：

- `infra_service`
- `logger_service`
- `netframe_service`
- `config_service`
- `event_service`
- `auth_service`
- `system_service`
- `network_service`
- `time_service`
- `media_service`
- `osd_service`
- `rtsp_service`
- `webrtc_service`
- `snapshot_service`
- `onvif_service`
- `alarm_service`
- `upgrade_service`
- `http_service`

后续可选模块：

- `abs_service`：根据网络状态和发送统计做码流自适应控制。
- `storage_service`：录像、图片和日志持久化管理。

依赖方向：

```text
app
  -> all services

all services
  -> infra_service

auth/config/system/network/time/upgrade/http
  -> logger_service

http/rtsp/onvif/webrtc
  -> netframe_service

rtsp/webrtc/snapshot/onvif
  -> media_service public interface

media_service
  -> hisi sdk adapter internally only
```

禁止规则：

- 禁止业务模块直接调用 `pthread`、`unistd`、`clock_gettime`、文件 API、系统时间 API，必须通过 `infra_service`。
- 禁止业务模块 include 其他模块的 `src/` 或私有头文件。
- 禁止非 `media_service` 模块直接调用海思 MPP/SDK。
- 禁止媒体帧通过 `event_service` 分发。
- 禁止 `config_service` 之外的模块直接读写配置文件。
- 禁止普通运行日志接口记录用户密码、token、密钥和认证头。
- 禁止 `logger_service` 记录敏感明文。
- 禁止从零实现完整 WebRTC 协议栈，必须封装成熟 WebRTC 库。

## 3. 软件设计硬性约束

本章是所有后续设计、编码、评审和 AI 生成代码必须遵守的统一标准。若本章与后文局部描述冲突，以本章为准。

### 3.1 架构边界约束

- 系统采用单进程、多线程、多静态库架构。
- 每个 service 必须有清晰职责边界，不能把多个业务域混在一个模块内。
- `infra_service` 只能提供无业务语义的基础能力，不能依赖任何业务 service。
- `logger_service` 只负责用户操作审计，不能替代普通运行日志。
- 普通运行日志只能通过 `infra::Log`。
- 用户操作审计只能通过 `logger_service`。
- `netframe_service` 只提供网络基础框架，不处理 HTTP、RTSP、ONVIF、WebRTC 业务语义。
- `config_service` 是唯一配置读写入口，其他模块不能直接读写配置文件。
- `media_service` 是唯一能直接调用海思 MPP/SDK 的模块。
- RTSP、WebRTC、ONVIF、Snapshot、录像等模块只能通过 `media_service` public interface 获取媒体能力。
- `event_service` 只传状态事件和控制事件，禁止传媒体帧。

### 3.2 依赖方向约束

- 所有 service 可以依赖 `infra_service`。
- 业务 service 不允许反向依赖 `app`。
- `infra_service` 不允许依赖任何业务 service。
- `logger_service` 可以依赖 `infra_service`，不能依赖 `config_service`、`event_service`、`http_service` 等业务模块。
- `media_service` 内部可以依赖海思 SDK，外部 public header 禁止暴露海思 SDK 类型。
- 协议 service 不能直接依赖海思 SDK。
- 不允许跨模块 include 其他模块 `src/` 或私有头文件。
- 模块之间只能通过 public header、接口类、工厂函数和稳定公共类型通信。
- 禁止循环依赖；如果出现双向调用，必须通过接口拆分、事件通知或上层编排解决。

### 3.3 对外接口约束

- 所有 public API 必须放在对应 service 的 `include/`。
- public header 只允许放接口类、公共 enum、公共 struct、工厂函数和稳定值类型。
- public header 禁止暴露实现类、private 成员变量、线程对象、锁、文件句柄、平台句柄、SDK 类型和具体存储后端。
- 对外 service 接口使用 `I<ServiceName>` 命名，例如 `ILoggerService`、`IMediaService`。
- 实现类使用 `XxxServiceImpl` 命名，且只能放在 `src/`。
- service 对象通过工厂函数创建，例如 `CreateLoggerService()`。
- 工厂函数返回 `std::unique_ptr<I...>`，调用方不感知实现类。
- 所有 public API 返回 `infra::Status` 或 `infra::Result<T>`。
- public API 禁止抛异常，禁止要求调用方捕获异常。
- public header 禁止 `using namespace`。
- public header 必须能单独 include 编译。

### 3.4 生命周期约束

- 所有 service 必须实现 `infra::IService`。
- 构造函数只做轻量初始化，不启动线程、不打开设备、不监听端口。
- `Init()` 负责资源准备和依赖检查。
- `Start()` 负责启动线程、定时器、监听、媒体管线等运行态资源。
- `Stop()` 负责停止接收新任务、停止线程、取消定时器、断开 session。
- `Deinit()` 负责释放 Init 阶段申请的资源。
- 停止顺序必须与启动顺序相反。
- `Stop()` 和 `Deinit()` 必须允许重复调用或安全忽略重复调用。
- 析构函数只做兜底清理，不能依赖析构函数完成主流程停机。
- 复杂对象必须有明确状态机，禁止靠散落的 bool 标志隐式表达生命周期。

### 3.5 资源管理约束

- 优先使用 RAII 管理资源。
- 禁止业务代码裸 `new/delete` 管理普通对象；优先使用 `std::unique_ptr`。
- 共享音视频数据使用引用计数 buffer，例如 `std::shared_ptr<IMediaBuffer>`。
- 编码后码流内存采用“分档内存池 + 引用计数 EncodedFrame”方案，禁止在跨模块分发路径中反复复制 payload。
- 跨线程回调中引用 session 时优先使用 `std::weak_ptr` 避免循环引用。
- C SDK handle、文件句柄、socket、线程、锁必须封装成 RAII 类或由 service 生命周期管理。
- 高频对象如媒体帧、RTP 包、事件对象后续可引入对象池，但对象池必须有容量上限。
- 媒体 buffer pool 必须有容量、档位、峰值、申请失败和丢帧统计。
- 嵌入式设备上禁止无上限内存增长。

### 3.6 并发和线程约束

- 编码线程禁止等待网络发送。
- 网络 IO 回调禁止执行耗时业务逻辑，只能做轻量解析和任务投递。
- 事件回调禁止执行耗时逻辑，只能投递到对应 service 的 `TaskQueue`。
- 定时器回调禁止执行耗时逻辑。
- 锁内禁止调用外部 service 的复杂接口。
- 每个网络客户端必须有独立有界发送队列。
- 队列必须定义满载策略，例如丢旧帧、丢新帧、断开慢客户端或返回 `kBusy`。
- 慢客户端只能影响自己的 session，不能影响编码线程和其他客户端。
- 跨线程共享数据必须有明确所有权、锁保护或无锁设计说明。
- 线程退出必须可控，`Stop()` 必须等待内部线程退出或进入不可再回调状态。

### 3.7 媒体链路约束

- 流媒体生产者命名固定为 `FrameSource`。
- 流媒体消费者命名固定为 `FrameSink`。
- 禁止使用 `FrameHub`、`FrameConsumer` 等混淆命名。
- `EncodedFrame` 只表达编码帧元数据、buffer 引用、offset 和 size，不包含 RTSP、WebRTC、ONVIF 协议字段。
- RTSP、WebRTC、Snapshot、Record 队列只能保存 `EncodedFrame` 或其引用，不能复制整帧 payload。
- 媒体帧不走 `event_service`。
- 多客户端共享同一路编码输出，不能为每个客户端创建独立硬件编码器。
- 同一帧编码数据只允许生产一份，多个 `FrameSink` 共享同一个 `IMediaBuffer` 引用。
- 发送端能使用 scatter-gather 时优先使用 payload slice，避免把 RTP/WebRTC 头和视频 payload 拼成新大包。
- 新客户端加入或丢包恢复时，通过 `RequestKeyFrame()` 请求关键帧。
- 抓图不能阻塞编码线程。
- 编码参数变更必须进入明确的 reconfiguring 状态，并发布状态事件。

### 3.8 配置约束

- 配置文件只能由 `config_service` 读写。
- 配置修改必须先校验，再应用，再持久化，再发布事件。
- 配置写入必须串行化，避免并发覆盖。
- 配置应用失败必须返回明确错误，并记录失败原因。
- 敏感配置如密码、token、密钥不能明文写日志或操作审计。
- 配置 schema 必须包含字段类型、默认值、合法范围、是否支持运行时修改。
- 运行时配置和持久化配置必须保持一致；无法保持一致时必须返回失败或进入回滚策略。

### 3.9 错误处理约束

- 所有 public API 使用 `infra::Status` 或 `infra::Result<T>` 表达结果。
- 禁止使用异常作为跨模块错误机制。
- 错误码必须稳定，不能随意改枚举含义。
- 错误必须能转换为稳定字符串。
- 不允许吞掉错误；无法处理的错误必须向上返回或记录运行日志。
- 可恢复错误不能导致进程退出。
- 不可恢复错误必须记录运行日志，并由 `system_service` 决定安全退出或设备重启。
- HTTP、ONVIF、RTSP 等对外协议必须有错误码映射策略。

### 3.10 日志和审计约束

- 普通运行日志用于诊断和开发，不记录用户敏感明文。
- 用户操作审计用于记录用户行为，必须通过 `logger_service`。
- 登录、登出、认证失败、token 过期、修改配置、重启、恢复出厂、升级、校时、网络修改、权限拒绝必须写操作审计。
- 操作审计必须记录用户、session、客户端 IP、模块、动作、目标、结果和失败原因。
- 操作审计存储可能位于 flash，必须考虑写失败、掉电、容量上限和写放大。
- 操作审计队列满或存储失败时，必须通过 `infra::Log` 记录错误并维护丢失计数。
- debug/trace 运行日志可以在队列满时丢弃，操作审计不能随意丢弃。

### 3.11 安全约束

- 未登录用户不能修改配置。
- 普通用户不能升级、重启、恢复出厂或修改管理员配置。
- 所有管理操作必须携带 `RequestContext`。
- 密码、token、密钥、认证头、升级包签名私钥禁止写入运行日志和操作审计。
- 对外输入必须做长度、类型、范围和权限校验。
- Web、RTSP、ONVIF、WebSocket 必须复用统一鉴权能力。
- 文件路径输入必须防止路径穿越。
- 升级包必须校验版本、完整性和签名。

### 3.12 构建和测试约束

- 构建系统统一使用 Makefile。
- 每个静态库必须支持在自身目录单独 `make`、`make clean`、`make test`。
- 顶层 `make all` 必须按依赖顺序编译所有静态库并链接 app。
- 构建输出统一进入 `build/`，不能污染源码目录。
- 默认编译选项包含 `-std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti`。
- 每个 public header 必须有 include 测试。
- 每新增一个模块，必须至少提供 header include 测试和最小生命周期测试。
- 修改公共接口必须同步更新测试和设计文档。
- 不允许提交无法通过 `make all` 和 `make test` 的代码。

### 3.13 第三方库和平台约束

- 不引入文档未明确允许的第三方库。
- WebRTC 必须使用成熟协议栈库，不从零实现 ICE、DTLS、SRTP、NACK 等协议细节。
- 第三方库 API 不能泄漏到本项目 public header。
- 海思 SDK API 只能出现在 `media_service` 内部 HAL。
- 平台相关实现放入对应 platform 或模块内部适配层。
- 后续支持其他平台时，只新增 platform 实现，不修改业务 service public API。

## 4. 工程目录和构建

顶层目录：

```text
app/
├── Makefile
├── app/
│   └── main.cpp
├── libs/
│   ├── infra_service/
│   ├── logger_service/
│   ├── netframe_service/
│   ├── config_service/
│   ├── event_service/
│   ├── auth_service/
│   ├── system_service/
│   ├── network_service/
│   ├── time_service/
│   ├── media_service/
│   ├── osd_service/
│   ├── rtsp_service/
│   ├── webrtc_service/
│   ├── snapshot_service/
│   ├── onvif_service/
│   ├── alarm_service/
│   ├── upgrade_service/
│   └── http_service/
├── configs/
├── third_party/
├── web/
├── tests/
└── build/
```

每个静态库固定结构：

```text
libs/xxx_service/
├── include/
│   └── xxx_service.h
├── src/
├── tests/
├── module.mk
└── Makefile
```

### 对外接口与实现分离原则

每个 service 必须遵守接口与实现分离：

- `include/` 只放对外 public API，包括接口类、公共 enum、公共 struct、工厂函数。
- `src/` 放实现类、私有 helper、具体存储后端、平台适配和内部状态。
- 对外接口使用 `I<ServiceName>` 命名，例如 `ILoggerService`、`IMediaService`。
- 实现类使用 `XxxServiceImpl` 命名，且只能放在 `src/`。
- 其他模块只能 include 当前模块 `include/` 下的 public header。
- 禁止其他模块 include 当前模块 `src/` 下的内部头文件。
- public header 不能暴露 private 成员变量、线程对象、锁、文件句柄、平台句柄或第三方 SDK 类型。
- 如需创建 service 对象，通过工厂函数创建，例如 `std::unique_ptr<ILoggerService> CreateLoggerService(const LoggerServiceConfig& config)`。
- public header 可以暴露稳定值类型，例如配置结构、查询条件、结果结构；不能暴露具体实现后端，例如 `FileOperationLogStore`。

构建规则：

- 顶层 `Makefile` include 每个库的 `module.mk`，统一编译完整工程。
- 每个 `libs/xxx_service/Makefile` 支持当前库单独编译。
- 单独编译输出仍放到顶层 `build/`，不能污染源码目录。
- 每个库至少支持 `make`、`make clean`、`make test`。

示例：

```sh
make all
make test
make -C libs/infra_service
make -C libs/logger_service test
make -C libs/media_service
```

公共编译变量：

```make
CXX ?= g++
AR ?= ar

BUILD_DIR := build
LIB_DIR := $(BUILD_DIR)/lib
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

CXXFLAGS += -std=c++17
CXXFLAGS += -Wall -Wextra -Werror
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-rtti
CXXFLAGS += -Ilibs/infra_service/include
```

构建顺序：

```text
infra_service
logger_service
netframe_service
config_service
event_service
auth_service
system_service
network_service
time_service
media_service
osd_service
rtsp_service
webrtc_service
snapshot_service
onvif_service
alarm_service
upgrade_service
http_service
app
```

## 5. 公共基础接口

所有 service 实现统一生命周期接口：

```cpp
class IService {
 public:
    virtual ~IService() = default;

    virtual Status Init() = 0;
    virtual Status Start() = 0;
    virtual void Stop() = 0;
    virtual void Deinit() = 0;
    virtual const char* Name() const = 0;
};
```

所有 public API 返回 `Status` 或 `Result<T>`，禁止抛异常。

```cpp
enum class Status {
    kOk = 0,
    kInvalidParam,
    kNotFound,
    kNoPermission,
    kUnauthorized,
    kTimeout,
    kBusy,
    kNotSupported,
    kIoError,
    kInternalError,
};

template <typename T>
struct Result {
    Status error;
    T value;
};
```

请求上下文用于用户操作审计：

```cpp
struct RequestContext {
    std::string request_id;
    std::string user_name;
    std::string session_id;
    std::string client_ip;
    std::string user_agent;
};
```

## 6. infra_service

定位：唯一公共基础库，不承载业务语义。

职责：

- `Status`、`Result<T>`、`IService`、基础类型。
- 线程、锁、条件变量、原子操作。
- `ITaskExecutor`。
- `TaskQueue`。
- `ThreadPool`。
- `TimerManager`。
- 单调时钟和系统时间封装。
- 文件、路径、字符串工具。
- 流媒体公共基础类型，例如 `StreamId`、`EncodedFrame`、`IMediaBuffer`。
- 普通运行日志 `infra::Log`。
- 平台适配。

不负责：

- 不记录用户操作审计。
- 不理解登录、配置修改、重启、升级等业务语义。
- 不依赖任何业务 service。

建议目录：

```text
libs/infra_service/
├── include/
│   └── infra/
│       ├── status.h
│       ├── status.h
│       ├── service.h
│       ├── request_context.h
│       ├── executor.h
│       ├── task_queue.h
│       ├── thread_pool.h
│       ├── timer_manager.h
│       ├── time.h
│       ├── file.h
│       ├── path.h
│       ├── log.h
│       ├── log_config.h
│       ├── stream_types.h
│       ├── media_buffer.h
│       ├── encoded_frame.h
│       └── noncopyable.h
├── src/
├── platform/
│   └── linux/
├── tests/
├── module.mk
└── Makefile
```

### 6.0 public header 注释标准

`infra_service` 的 public header 是后续所有业务模块依赖的稳定契约，必须使用中文注释说明接口含义。

要求：

- 每个 public header 文件开头必须包含版权、作者、文件名和用途说明。
- 版权作者统一为 `Copyright (c) 2026 CBinary`、`Author: CBinary`。
- 每个 public 类型必须说明作用、适用场景、所有权、线程安全和模块边界。
- 每个 public 函数必须说明函数作用、参数意义、返回值、错误码和注意事项。
- public struct 字段必须说明单位、默认值或业务含义。
- 注释只能描述 public API 契约，不能泄漏 `src/` 内部实现类、平台句柄或第三方 SDK 类型。

`TaskQueue` 与 `ThreadPool` 区分：

- `TaskQueue`：单 worker、FIFO、串行执行，适合 service 内部状态机。
- `ThreadPool`：多 worker、并发执行，不保证完成顺序，适合可并行的通用后台任务。
- `ITaskExecutor`：抽象异步任务投递能力，`TaskQueue` 和 `ThreadPool` 都必须实现该接口。
- `TimerManager`：一个调度线程只监听定时器到期，回调必须投递到 `ITaskExecutor` 异步执行。

`TimerManager` 约束：

- 调度线程禁止直接执行业务回调。
- 默认创建内部 callback 线程池；service 需要串行状态机时，可以传入自己的 `TaskQueue` 作为 callback executor。
- 周期定时器默认使用 `kSkipIfRunning`，上一轮回调仍在执行或排队时跳过本次回调，避免慢回调堆积。
- `Cancel()` 只保证取消未到期或未投递的定时器，已经投递到 executor 的 callback 不撤回。
- callback executor 队列满导致投递失败时，必须记录运行日志并更新统计。

### 6.1 infra::Log

`infra::Log` 只用于普通运行日志输出。

```cpp
namespace infra {

enum class LogLevel {
    kTrace,
    kDebug,
    kInfo,
    kWarn,
    kError,
    kFatal,
};

struct LogConfig {
    LogLevel min_level;
    std::string file_path;
    uint32_t max_file_size_kb;
    uint32_t max_file_count;
    bool console_output;
    bool async_write;
};

class Log {
 public:
    static Status Init(const LogConfig& config);
    static void Shutdown();

    static void Write(LogLevel level,
                      const char* module,
                      const char* file,
                      int line,
                      const char* fmt,
                      ...);
};

}  // namespace infra
```

要求：

- 支持控制台输出、文件输出、等级过滤、模块标识和日志轮转。
- 支持异步写入，避免阻塞媒体线程和网络线程。
- 队列满时可以丢弃 debug/trace 日志。
- 不提供用户操作查询接口。

建议宏：

```cpp
#define INFRA_LOG_INFO(module, fmt, ...) \
    infra::Log::Write(infra::LogLevel::kInfo, module, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
```

## 7. logger_service

定位：用户操作审计模块，独立于普通运行日志。

职责：

- 记录用户当前操作。
- 支持查询、导出操作审计记录。
- 后端存储通过接口抽象，默认文件实现，后续可替换为 flash ring buffer 或 raw flash 分区实现。
- 依赖 `infra_service` 的文件、路径、错误码和 `Result<T>`；当前实现未使用后台队列。

必须记录的操作：

- 登录、登出、认证失败、token 过期。
- 修改视频、音频、图像、RTSP、WebRTC、ONVIF、网络、时间、OSD、用户等配置。
- 重启、恢复出厂、升级、手动校时、NTP 配置修改。
- 权限拒绝，例如普通用户尝试升级、恢复出厂或修改管理员配置。

建议目录：

```text
libs/logger_service/
├── include/
│   └── logger_service.h
├── src/
│   ├── logger_service.cpp
│   ├── operation_log_store.h
│   ├── file_operation_log_store.cpp
│   ├── operation_record_codec.h
│   └── operation_record_codec.cpp
├── tests/
├── module.mk
└── Makefile
```

公共类型：

```cpp
enum class OperationAction {
    kLogin,
    kLogout,
    kAuthFailed,
    kTokenExpired,
    kModifyConfig,
    kReboot,
    kFactoryReset,
    kUpgrade,
    kTimeSync,
    kNetworkChange,
    kUserManage,
    kPermissionDenied,
};

enum class OperationResult {
    kSuccess,
    kFailed,
    kRejected,
};

struct OperationRecord {
    int64_t timestamp_ms;
    std::string request_id;
    std::string user_name;
    std::string session_id;
    std::string client_ip;
    std::string module;
    OperationAction action;
    std::string target;
    OperationResult result;
    std::string reason;
};

struct OperationLogQuery {
    int64_t begin_timestamp_ms;
    int64_t end_timestamp_ms;
    std::string user_name;
    OperationAction action;
    OperationResult result;
    bool has_action;
    bool has_result;
    uint32_t limit;
};

struct OperationLogExportOptions {
    std::string output_path;
    OperationLogQuery query;
};
```

存储抽象：

```cpp
class IOperationLogStore {
 public:
    virtual ~IOperationLogStore() = default;

    virtual Status Open() = 0;
    virtual void Close() = 0;
    virtual Status Append(const OperationRecord& record) = 0;
    virtual Result<std::vector<OperationRecord>> Query(
        const OperationLogQuery& query) = 0;
    virtual Status Export(const OperationLogExportOptions& options) = 0;
};
```

service 接口：

```cpp
class ILoggerService : public IService {
 public:
    virtual Status RecordOperation(const OperationRecord& record) = 0;

    virtual Result<std::vector<OperationRecord>> QueryOperations(
        const OperationLogQuery& query) = 0;

    virtual Status ExportOperations(
        const OperationLogExportOptions& options) = 0;
};
```

当前源码设计：

- public header 只暴露 `ILoggerService`、`LoggerServiceConfig`、`OperationRecord`、查询/导出参数和枚举转换函数。
- `LoggerServiceImpl` 位于 `src/logger_service.cpp`，由 `CreateLoggerService()` 创建，不暴露到 public header。
- `IOperationLogStore` 和 `FileOperationLogStore` 是 `src/operation_log_store.h` 内部接口与文件后端，业务模块不能 include。
- `FileOperationLogStore` 使用 JSON Lines 持久化，`operation_record_codec` 负责编码、解码和字符串到枚举转换。
- `QueryOperations()` 从当前文件和轮转文件按新到旧扫描，支持时间范围、用户名、动作、结果和 limit 过滤。
- `ExportOperations()` 复用 query 结果，导出为 JSON Lines 文件。
- 文件轮转由 `LoggerServiceConfig::max_file_size_bytes` 和 `max_rotate_files` 控制。
- 当前 `RecordOperation()` 同步调用 store append；`queue_capacity` 已在配置中预留，但尚未接入异步队列。

flash 设计原则：

- v1 只实现 `IOperationLogStore` 抽象和文件后端。
- 后续如果需要直接写 flash 分区，新增 `FlashRingOperationLogStore`，业务模块不改。
- 后续应接入有界异步队列，减少业务线程被文件系统写入阻塞。
- 队列满或存储失败时，必须通过 `infra::Log` 打印错误，并维护丢失计数。
- 操作审计日志优先保留，不能像 debug 日志一样随意丢弃。
- 敏感字段必须脱敏或不记录。
- 当前 `make -C libs/logger_service test` 已验证 lifecycle、写入、查询、导出和 stopped 后拒绝写入。

## 8. netframe_service

定位：网络基础框架。

职责：

- 封装 socket、epoll、非阻塞 IO。
- 提供 EventLoop、Poller、Timer、TcpServer、UdpSocket 基础能力。
- 提供多线程 Reactor 模型。
- 不处理 HTTP、RTSP、ONVIF、WebRTC 业务语义。

推荐模型：

```text
epoll
+ non-blocking socket
+ event loop thread
+ worker task queue
+ per-session state machine
+ bounded send queue
```

规则：

- 不采用一连接一线程。
- 不把所有协议塞进一个大线程。
- IO 回调只做轻量解析和任务投递，耗时业务进入对应 service 的 `TaskQueue`。

当前 public API：

- `INetframeService` 负责创建 `IEventLoop`、`ITcpServer`、`IUdpSocket` 和 `IReactor`。
- `IEventLoop` 提供 `Start()`、`Stop()`、任务投递、一次性定时器、周期定时器、取消定时器、统计查询和名称。
- `ITcpServer` 提供监听、连接回调、消息回调、关闭回调、local address 和统计查询。
- `ITcpConnection` 提供连接 ID、地址、`Send()`、`Close()` 和待发送字节数。
- `IUdpSocket` 提供 UDP 绑定、发送、接收回调、local address 和统计查询。
- `IReactor` 管理多个 event loop，并通过 round-robin 返回下一个 loop。

当前源码机制：

- `EventLoopImpl` 使用 `epoll_create1`、非阻塞 wakeup pipe 和一个 loop thread；`Post()` 把任务放入有界队列并唤醒 epoll。
- 定时器由 loop 内部 `std::map<TimerId, TimerInfo>` 管理，`RunAfter()` 和 `RunEvery()` 共享 `AddTimer()`；周期定时器到期后更新下一次触发时间。
- TCP server 使用非阻塞 `accept4` 接收连接，每个连接由 `TcpConnectionImpl` 管理，读写事件都挂在同一个 event loop。
- TCP 每连接有有界发送队列和 pending bytes 限制；发送队列满时返回 `kBusy`，并按 `send_stall_timeout_ms` 判断是否关闭慢客户端。
- UDP socket 使用非阻塞 fd，在 event loop 中接收 datagram，通过 callback 上报。
- `ReactorImpl` 按配置创建多个 `EventLoopImpl`，`NextLoop()` 使用原子计数 round-robin 分配。

当前源码状态：

- `netframe_service` public header 已声明 `EventLoopStats`、`TcpServerStats`、`UdpSocketStats` 和 `GetStats()`；当前 `EventLoopImpl`、`TcpServerImpl`、`UdpSocketImpl` 都已实现统计查询。
- TCP server 通过 `TcpStatsSink` 汇总连接数、读写字节、发送队列忙、慢客户端关闭和发送投递失败等统计。
- 当前 `make -C libs/netframe_service test` 已覆盖 event loop、self stop、reactor、TCP/UDP loopback、发送队列、发送 rollback 和慢客户端统计。
- Linux socket/epoll/unistd 调用只允许留在 `netframe_service` 内部平台实现边界，HTTP、RTSP、ONVIF、WebRTC 等业务模块不能复制该模式。

## 9. config_service

定位：全局配置中心。

职责：

- 管理视频、音频、图像、RTSP、WebRTC、ONVIF、抓图、网络、时间、OSD、告警、存储、用户、系统、日志配置。
- 负责配置校验、持久化、运行时应用和变更通知。
- 维护配置版本，避免并发写覆盖。
- 配置修改成功、失败或被拒绝时，调用 `logger_service` 写操作审计。

配置分组：

```text
video
audio
image
rtsp
webrtc
onvif
snapshot
network
time
osd
alarm
storage
user
system
log
logger
```

规则：

- 其他模块不能直接读写配置文件。
- 配置修改先校验，再应用，再持久化，再发布事件。
- 配置应用失败必须返回明确错误，并记录失败原因。
- 写配置使用串行队列。

## 10. event_service

定位：进程内事件发布订阅。

职责：

- 分发状态事件和控制事件。
- 解耦配置变更、客户端连接、码流状态、时间变化、升级进度等通知。
- 异步派发，避免发布者阻塞。

事件

分事件开始，事件结束等

事件类型：

```text
ConfigChanged
MediaPipelineStarted
MediaPipelineStopped
MediaPipelineError
StreamStarted
StreamStopped
RtspClientConnected
RtspClientDisconnected
WebRtcClientConnected
WebRtcClientDisconnected
OnvifRequestReceived
SnapshotCreated
TimeChanged
NetworkChanged
AlarmTriggered
StorageStateChanged
SystemStatusChanged
UpgradeProgressChanged
```

规则：

- 媒体帧不走 `event_service`。
- 事件处理失败不能影响事件发布者。
- 事件回调不能执行耗时操作，只能投递到对应 service 的 `TaskQueue`。

当前源码设计：

- `IEventService` 只暴露订阅、取消订阅和发布接口，工厂函数为 `CreateEventService()`。
- `Event` 只包含 `type`、`source`、`target`、`message` 和 `value`，用于轻量状态/control metadata。
- `EventServiceImpl` 使用 `infra::TaskQueue` 创建名为 `event-thread` 的异步派发线程。
- 生命周期状态包含 `Created`、`Initialized`、`Started`、`Stopped`、`Deinitialized`，`Start()` 后才允许 `Publish()`。
- 订阅表以 `EventSubscriptionId` 为 key，当前最大订阅数为 128。
- `Publish()` 会复制匹配事件类型的 handler 列表，并把执行 handler 的任务投递到 `event-thread`。
- 事件字段做长度约束：source 最大 64，target 最大 128，message 最大 256；超限返回 `kInvalidParam`。
- 当前 handler 在 `event-thread` 顺序执行；如果 handler 内部阻塞，会影响后续事件派发，因此业务逻辑必须再投递到订阅者自己的队列。
- 当前 `make -C libs/event_service test` 已验证 lifecycle、订阅发布、取消订阅、订阅上限、字段长度和错误路径。

## 11. media_service

定位：媒体采集、处理、编码和编码帧分发核心模块。

职责：

- 唯一直接调用海思 MPP/SDK。
- 管理 VI、VPSS、VENC、AENC 等媒体资源。
- 生成主码流、子码流、抓图码流和音频流。
- 动态应用编码参数、帧率、码率、GOP、分辨率。
- 向 RTSP、WebRTC、ONVIF、抓图、录像分发编码帧。

### 11.1 媒体对象模型

术语固定：

- `FrameSource`：每路码流的生产者，属于 `media_service` 内部对象。
- `FrameSink`：编码帧消费者，由 RTSP、WebRTC、抓图、录像等模块实现。
- `FrameSubscription`：一次订阅关系，包含订阅 ID、stream ID、队列策略。
- `IMediaBuffer`：跨模块共享的音视频数据 buffer 接口，使用引用计数管理生命周期。
- `EncodedFrame`：跨模块传递的编码帧，包含元数据、buffer 引用、offset 和 size。
- `MediaBufferPool`：`media_service` 内部的分档内存池，用于管理编码后码流 buffer。
- `StreamChannel`：一路码流通道，内部持有 `FrameSource` 和编码器状态。

媒体路径：

```text
HisiAdapter
  -> MediaPipeline
  -> StreamChannel
  -> MediaBufferPool
  -> FrameSource
  -> FrameSink
```

禁止混用旧命名：

- 不使用 `FrameHub` 表示生产者。
- 不使用 `FrameConsumer` 表示消费者。
- 统一使用 `FrameSource` 和 `FrameSink`。

码流：

- `main-stream`：高清主码流，用于 RTSP、WebRTC 高清预览、录像。
- `sub-stream`：低码率子码流，用于 WebRTC 低清预览、移动端预览。
- `snapshot-stream`：可选抓图码流，可按需启动，也可复用主码流或子码流。

内存设计：

- 采用“分档内存池 + 引用计数 EncodedFrame”。
- 海思 VB/MMZ 硬件内存只在 `media_service` 内部 HAL 封装，不暴露到 public header。
- `infra_service` 只定义 `IMediaBuffer`、`EncodedFrame`、`StreamId`、`VideoCodec`、`FrameType` 等稳定公共类型。
- `media_service` 内部实现 `MediaBufferPool`，按容量档位管理编码后码流内存。
- 推荐初始档位：64KB、256KB、1MB、2MB、4MB；具体数量由设备内存、分辨率、码率和并发数调参。
- 音频、小码流、主码流、JPEG 可使用不同档位或独立 pool，但对外统一表现为 `IMediaBuffer`。
- `FrameSource` 分发 `EncodedFrame` 时只复制轻量元数据和引用，不复制 payload。
- `FrameSink` 队列满时按策略丢帧或断开慢客户端，不能扩容导致内存无上限增长。
- `MediaBufferPool` 必须统计总 buffer 数、使用中数量、峰值、申请失败次数、丢帧次数。

### 11.2 公共媒体类型

```cpp
enum class StreamId {
    kMain,
    kSub,
    kSnapshot,
};

enum class StreamState {
    kClosed,
    kOpening,
    kRunning,
    kReconfiguring,
    kError,
};

enum class VideoCodec {
    kH264,
    kH265,
    kMjpeg,
};

enum class FrameType {
    kIdr,
    kI,
    kP,
    kB,
    kJpeg,
};

enum class FrameDropPolicy {
    kDropOldest,
    kDropNewest,
    kDisconnectSlowSink,
};

enum class ConfigApplyMode {
    kRuntimeOnly,
    kRuntimeAndPersist,
};

enum class KeyFrameReason {
    kClientJoined,
    kPacketLoss,
    kSnapshot,
    kManual,
};

using FrameSubscriptionId = uint64_t;
```

```cpp
struct StreamDescriptor {
    StreamId stream_id;
    std::string name;
    VideoCodec codec;
    std::string resolution;
    uint32_t max_fps;
    bool supports_runtime_config;
};

struct StreamStatus {
    StreamId stream_id;
    StreamState state;
    uint32_t sink_count;
    uint64_t frame_count;
    uint64_t dropped_frame_count;
    std::string last_error;
};

struct FrameSubscribeOptions {
    StreamId stream_id;
    uint32_t queue_capacity;
    FrameDropPolicy drop_policy;
    bool require_key_frame_first;
    std::string sink_name;
};

struct EncodedFrame {
    StreamId stream_id;
    VideoCodec codec;
    FrameType frame_type;
    uint64_t sequence;
    int64_t pts_us;
    int64_t dts_us;
    std::shared_ptr<IMediaBuffer> buffer;
    uint32_t offset;
    uint32_t size;
};
```

### 11.3 FrameSink 接口

```cpp
class IFrameSink {
 public:
    virtual ~IFrameSink() = default;

    virtual const char* Name() const = 0;

    virtual void OnFrame(const EncodedFrame& frame) = 0;

    virtual void OnSourceStateChanged(StreamId stream_id,
                                      StreamState state) = 0;
};
```

规则：

- `OnFrame()` 不能阻塞编码线程。
- `FrameSink` 内部必须有自己的发送队列或任务队列。
- 慢 sink 只能丢自己的帧或断开，不能影响其他 sink。

### 11.4 IMediaService 接口

`IMediaService` 暴露业务级媒体控制接口，不暴露内部 HAL、`FrameSource` 或 `StreamChannel`。

```cpp
class IMediaService : public IService {
 public:
    virtual Result<std::vector<StreamDescriptor>> ListStreams() const = 0;

    virtual Result<StreamStatus> GetStreamStatus(StreamId stream_id) const = 0;

    virtual Status OpenStream(StreamId stream_id) = 0;

    virtual Status CloseStream(StreamId stream_id) = 0;

    virtual Result<FrameSubscriptionId> AttachSink(
        const FrameSubscribeOptions& options,
        IFrameSink* sink) = 0;

    virtual Status DetachSink(FrameSubscriptionId subscription_id) = 0;

    virtual Status RequestKeyFrame(StreamId stream_id,
                                  KeyFrameReason reason) = 0;

    virtual Status ApplyEncoderConfig(StreamId stream_id,
                                     const VideoEncoderConfig& config,
                                     ConfigApplyMode mode) = 0;

    virtual Result<VideoEncoderConfig> GetEncoderConfig(
        StreamId stream_id) const = 0;

    virtual Result<MediaCapabilities> GetCapabilities() const = 0;

    virtual Result<MediaPipelineStatus> GetPipelineStatus() const = 0;
};
```

设计要求：

- RTSP、WebRTC、Snapshot 只能依赖 `IMediaService` 和 `IFrameSink`。
- `FrameSource`、`StreamChannel`、`HisiAdapter` 只在 `media_service` 内部可见。
- `OpenStream()` / `CloseStream()` 是码流资源状态管理，不代表每个客户端独占编码器。
- 同一路 stream 的多个 sink 共享同一路编码输出。
- `ApplyEncoderConfig(..., kRuntimeAndPersist)` 的持久化仍由 `config_service` 协调，`media_service` 不直接写配置文件。
- `GetCapabilities()` 是 Web、ONVIF 和后续自适应策略获取设备编码能力的唯一入口，能力来源必须在 `media_service` 内部由海思 SDK/MPP adapter 查询或缓存，外部模块不能维护静态分辨率、编码格式、帧率、码率范围表。

### 11.5 设备媒体能力模型

媒体能力用于描述当前硬件、sensor、VI/VPSS/VENC 组合在每路码流上可支持的参数集合。它不是配置值，而是配置校验和前端可选项的约束来源。

能力内容：

- 每路码流的 `stream_id`，当前至少包含 `main` 和 `sub`。
- 支持的编码格式与 profile，例如 H.264、H.265、MJPEG。
- 支持的分辨率集合，按海思 SDK/MPP 查询结果或产品约束过滤后输出。
- 帧率范围、码率范围、GOP 范围。
- 支持的码率控制模式，例如 CBR、VBR、FIXQP。
- 是否支持 smart codec 等平台增强能力。

设计边界：

- `media_service` public header 只暴露平台无关的 `MediaCapabilities`、`VideoStreamCapabilities`、`CodecCapability` 等值类型，禁止暴露海思 SDK 类型。
- `HisiAdapter`/`MppAdapter` 内部负责把 SDK 能力转换成平台无关结构；host/mock adapter 只能作为开发和测试兜底。
- `http_service` 只读取 `MediaService::GetCapabilities()` 并序列化为 JSON，不直接推断硬件能力。
- Web 前端通过 `GET /api/media/capabilities` 生成分辨率、编码格式、帧率、码率、GOP、码率控制和 smart codec 控件，不能保留固定枚举作为最终设备能力。
- `PUT /api/config/video` 必须用当前能力集校验提交参数；不支持的分辨率、编码格式、帧率、码率、GOP 或 smart codec 请求应返回 400。
- 真机阶段需要在 `media_service` 内部增加能力缓存刷新策略：启动时查询一次，sensor/pipe/编码器重建后刷新；接口对外保持同步查询语义。

## 12. 协议和业务服务

### 12.1 http_service

职责：

- 托管 Web 前端。
- 提供 REST API。
- 提供 WebSocket 状态推送。
- 提供 WebRTC 信令入口。
- 做认证、权限、参数校验和错误码转换。
- 生成 `RequestContext`，供配置、升级、重启等操作写审计日志。

REST API：

```text
GET    /api/system/info
GET    /api/system/capabilities
GET    /api/system/status

GET    /api/media/capabilities

GET    /api/config/video
PUT    /api/config/video
GET    /api/config/audio
PUT    /api/config/audio
GET    /api/config/image
PUT    /api/config/image
GET    /api/config/rtsp
PUT    /api/config/rtsp
GET    /api/config/webrtc
PUT    /api/config/webrtc
GET    /api/config/onvif
PUT    /api/config/onvif
GET    /api/config/network
PUT    /api/config/network
GET    /api/config/time
PUT    /api/config/time

POST   /api/time/sync

POST   /api/webrtc/peers
POST   /api/webrtc/peers/{peer_id}/offer
POST   /api/webrtc/peers/{peer_id}/candidate
DELETE /api/webrtc/peers/{peer_id}

GET    /api/snapshot/main.jpg
GET    /api/snapshot/sub.jpg

GET    /api/status/streams
GET    /api/status/clients

GET    /api/logs/export
GET    /api/operations
GET    /api/operations/export

POST   /api/upgrade
POST   /api/system/reboot
POST   /api/system/factory-reset
```

说明：

- `/api/media/capabilities` 返回 `media_service` 提供的设备媒体能力，供 Web 参数页面生成可选项，也供 HTTP 侧保存视频配置前做能力校验。
- `/api/logs/export` 导出普通运行日志，由 `infra::Log` 或诊断门面提供数据。
- `/api/operations` 和 `/api/operations/export` 查询、导出用户操作审计，由 `logger_service` 提供数据。

### 12.2 rtsp_service

职责：

- 提供 RTSP Server。
- 支持 `/live/main`、`/live/sub`。
- 支持 RTP over UDP 和 RTP over TCP interleaved。
- 支持 RTSP 鉴权。
- 实现 `IRtspFrameSink`，通过 `IRtspFrameSource` 从媒体侧订阅编码帧；当前源码保留 RTSP 私有 frame source/sink 接口，后续应与第 11 章 `IMediaService`/`IFrameSink` 统一。
- 管理多个 RTSP client session。

当前源码设计：

- public API 已提供 `IRtspService`、`IRtspFrameSource`、`IRtspFrameSink`、`IRtspAdaptiveObserver`、`RtspServiceOptions`、`RtspServiceDependencies` 和 `CreateRtspService()`。
- `RtspServiceImpl` 同时实现 `IRtspService` 和 `IRtspFrameSink`，内部依赖 `netframe_service` 创建 event loop、TCP server 和 UDP socket。
- 生命周期遵守 `IService`：`Init()` 校验 `netframe_service`、session 数、MTU 和 request 大小；`Start()` 创建 loop/server/socket 并向 frame source 订阅主/子码流；`Stop()` 反向释放 frame source、TCP server、UDP socket、event loop 和 session 表。
- RTSP session 状态包括 `kInit`、`kReady`、`kPlaying`、`kClosed`。`SETUP` 进入 ready，`PLAY` 进入 playing，`TEARDOWN` 关闭连接。
- 支持 `OPTIONS`、`DESCRIBE`、`SETUP`、`PLAY`、`TEARDOWN`。`DESCRIBE` 生成 SDP，当前 SDP payload type 固定按 H.264 输出。
- 支持 Basic 鉴权：开启 `enable_auth` 后解析 `Authorization: Basic ...`，调用 `IAuthService::Login()` 和 `CheckPermission(..., kPreviewVideo, stream_path)`。
- `PushFrame()`/`OnEncodedFrame()` 将 `EncodedFrame` 分发给相同 stream 且处于 playing 的 session；首帧必须等待 IDR/I 帧，否则丢帧并上报自适应事件。
- RTP 封包支持 H.264 FU-A 和 H.265 FU 分片；小帧直接封成单 RTP packet；TCP interleaved 使用 `$ + channel + length + RTP`，UDP 使用 client RTP port。
- 统计通过 `RtspServiceStats` 和 `RtspSessionStats` 暴露，包含 active/total session、鉴权失败、解析失败、TCP/UDP session、RTP packet/bytes、丢帧和慢客户端关闭。
- 事件与自适应：连接/断开通过 `event_service` 发布 `kRtspClientConnected`/`kRtspClientDisconnected`；丢帧、慢客户端关闭、关键帧请求和发送样本通过 `IRtspAdaptiveObserver` 上报。

当前测试覆盖：

- `rtsp_service_header_test`：legacy `RtspService::Name()` 兼容检查。
- `rtsp_service_tcp_interleaved_test`：OPTIONS/DESCRIBE/SETUP/PLAY 主路径、TCP interleaved RTP 输出和统计。
- `rtsp_service_udp_transport_test`：UDP client port 协商、RTP UDP 包发送和统计。
- `rtsp_service_auth_adaptive_test`：Basic 鉴权失败/成功、preview 权限检查、PLAY 后帧推送和 adaptive observer 回调。

当前限制：

- 尚未接入 `app/main.cpp` 的 service registry。
- 尚未直接接 `media_service` 的最终 `IMediaService`/`IFrameSink` 接口。
- 未完整实现 RTCP、RTP over UDP 的 RTCP 端口、session timeout 定时清理、复杂 SDP profile、Digest 鉴权和 H.265 SDP 描述。
- 发送队列背压依赖 `netframe_service` 返回 `kBusy`，当前只做丢帧统计、adaptive 通知和关闭慢 TCP client，尚未实现码率/帧率自适应执行动作。

### 12.3 webrtc_service

职责：

- 封装成熟 WebRTC 库。
- 管理 PeerConnection。
- 处理 SDP offer、answer、candidate。
- 实现 `IFrameSink`，从 `media_service` 订阅编码帧。
- 将 H.264/H.265 等帧送入 WebRTC 协议栈。
- 处理 PLI，转发关键帧请求给 `media_service`。

不实现：

- ICE
- STUN/TURN
- DTLS
- SRTP
- NACK
- 浏览器兼容细节

### 12.4 onvif_service

职责：

- 支持 WS-Discovery。
- 支持 Device Service。
- 支持 Media Service。
- 复用 `auth_service` 鉴权。
- 媒体配置修改进入 `config_service`。
- 码流地址返回 `rtsp_service` URL。
- 抓图地址返回 `snapshot_service` URL。

### 12.5 snapshot_service

职责：

- 提供 HTTP 抓图接口。
- 支持 ONVIF `GetSnapshotUri`。
- 默认复用主码流或子码流。
- 必要时请求 `media_service` 触发关键帧。
- 抓图请求不能阻塞媒体编码线程。

### 12.6 auth_service

职责：

- 管理用户、密码、角色、权限。
- 管理登录 token 和 session。
- 为 HTTP、WebSocket、RTSP、ONVIF 提供统一鉴权。
- 登录、登出、认证失败、token 过期和权限拒绝必须记录审计；当前通过 `IAuthAuditSink` 适配到 `logger_service`。

权限：

```text
admin       可修改配置、升级、重启、恢复出厂
operator    可查看状态、预览视频、部分配置
viewer      只读和预览
```

当前源码设计：

- `IAuthService` 提供 `Login()`、`Logout()`、`ValidateToken()`、`CheckPermission()` 和 `SetAuditSink()`。
- `IAuthUserStore` 隔离用户来源，当前有内存用户存储和 JSON 文件用户存储。
- `IPasswordVerifier` 隔离凭据校验，当前有明文校验器和 SHA-256 盐值凭据校验器。
- `AuthPrincipal` 不包含 token、密码和认证头，可传给其他模块做权限判断。
- `AuthAuditRecord` 不包含密码、token、认证头或 password credential，调用方可转换为 `logger_service::OperationRecord`。
- `AuthServiceImpl` 使用 mutex 保护 session map、生命周期状态和 audit sink 指针。
- token session 使用单调时钟判断过期，使用系统时间计算返回给调用方的 `expires_at_ms`。
- `CheckPermission()` 按角色判断权限：viewer 只能读状态和预览，operator 可修改配置，admin 拥有升级、重启、恢复出厂和用户管理等高权限。
- 明文密码校验器仅用于开发和测试；SHA-256 盐值凭据是当前骨架实现，生产阶段仍应替换为受控密码哈希与随机数方案。
- 当前 `make -C libs/auth_service test` 已验证登录、权限拒绝、认证失败审计不泄露敏感字、登出、token 过期、JSON 用户存储和 SHA-256 凭据。

### 12.7 system_service

职责：

- 管理 service 生命周期。
- 提供设备型号、序列号、固件版本。
- 提供系统能力集。
- 提供 CPU、内存、温度、网络状态。
- 支持重启、恢复出厂。
- 监控关键线程 heartbeat。
- 媒体线程异常时优先重启媒体管线。
- 不可恢复错误时记录运行日志，并安全退出或设备重启。
- 重启、恢复出厂必须调用 `logger_service` 写操作审计。

### 12.8 network_service

职责：

- 管理 DHCP、静态 IP、网关、DNS。
- 管理 Web、RTSP、ONVIF、WebRTC 端口配置。
- 提供 MAC、网卡状态、IP、链路状态。
- 网络变化后发布 `NetworkChanged`。
- 网络配置修改必须调用 `logger_service` 写操作审计。

### 12.9 time_service

职责：

- 管理系统时间、时区、NTP。
- 支持手动校时、ONVIF 校时、周期 NTP 同步。
- 时间变化后发布 `TimeChanged`。
- 手动校时、ONVIF 校时、NTP 配置修改必须调用 `logger_service` 写操作审计。

### 12.10 osd_service

职责：

- 管理时间水印、通道名、自定义文字。
- 管理 OSD 位置、颜色、显示开关。
- 配置来自 `config_service`。
- 最终应用到 `media_service`。

### 12.11 upgrade_service

职责：

- 支持 Web 上传升级包。
- 校验版本、签名、完整性。
- 执行升级流程。
- 发布升级进度。
- 升级开始、成功、失败必须调用 `logger_service` 写操作审计。

### 12.12 alarm_service

职责：

- 支持移动侦测、IO 输入、遮挡检测、网络异常告警。
- 告警事件通过 `event_service` 广播。
- 告警规则配置来自 `config_service`。

## 13. 关键流程

### 13.1 WebRTC 拉流

1. 浏览器访问 Web 页面。
2. 前端登录，`auth_service` 生成 session。
3. 前端创建 WebRTC offer。
4. `http_service` 接收信令并调用 `webrtc_service`。
5. `webrtc_service` 创建 `WebRtcPeerSession`。
6. WebRTC 库完成 SDP、ICE、DTLS、SRTP。
7. `WebRtcPeerSession` 作为 `IFrameSink` 调用 `IMediaService::AttachSink()`。
8. `media_service` 内部 `FrameSource` 分发 `EncodedFrame`。
9. WebRTC 库封装并发送给浏览器。
10. 断开后调用 `DetachSink()`，释放 session。

### 13.2 RTSP 拉流

1. 客户端请求 `rtsp://ip/live/main`。
2. `rtsp_service` 的 TCP server 接收连接，创建 session，发布 `RtspClientConnected`。
3. `OPTIONS` 返回支持的方法集合。
4. `DESCRIBE` 校验 `/live/main` 或 `/live/sub`，按需执行 Basic 鉴权和 `kPreviewVideo` 权限检查，返回 SDP。
5. `SETUP` 根据 `Transport` 选择 TCP interleaved 或 UDP client port，session 进入 ready。
6. `PLAY` 使 session 进入 playing，向 frame source 请求关键帧，并上报 `kKeyFrameRequested`。
7. `rtsp_service` 收到 `EncodedFrame` 后先等待 IDR/I 帧，再按 H.264/H.265 RTP 规则封包。
8. TCP interleaved 通过 `$` 帧发送；UDP 通过 `IUdpSocket::SendTo()` 发送到客户端 RTP port。
9. 发送失败时累计丢帧；`kBusy` 视为慢客户端，累计 slow close，通知 adaptive observer 并关闭 TCP 连接。
10. `TEARDOWN` 或连接断开后移除 session，发布 `RtspClientDisconnected`。

### 13.3 配置修改

1. Web、ONVIF 或其他入口提交配置修改。
2. 入口模块完成鉴权和参数解析，生成 `RequestContext`。
3. 如果修改视频配置，入口模块必须通过 `media_service::GetCapabilities()` 获取当前设备能力，并先校验编码格式、分辨率、帧率、码率、GOP、码率控制和 smart codec。
4. `config_service` 校验配置。
5. `config_service` 串行应用配置。
6. 相关业务模块执行运行时应用。
7. `config_service` 持久化配置。
8. `config_service` 发布 `ConfigChanged`。
9. `config_service` 调用 `logger_service` 写 `OperationRecord`。
10. `http_service` 通过 WebSocket 推送结果。

### 13.3.1 Web 视频能力发现与保存

1. Web 视频配置页面加载时请求 `GET /api/media/capabilities`。
2. `http_service` 调用 `media_service::GetCapabilities()`，将每路码流能力序列化为 JSON。
3. 前端按能力集生成编码格式、分辨率、帧率、码率、码率控制、GOP 和 smart codec 控件。
4. 用户保存时，前端先按能力集做本地校验，避免明显不支持的参数提交。
5. `http_service` 在 `PUT /api/config/video` 再做后端能力校验，前端校验只作为体验优化，不能作为可信边界。
6. 校验通过后进入 `config_service` 的配置写入流程；后续真机阶段再由 `media_service` 执行 VENC 动态重配和失败回滚。

### 13.4 时间同步

1. Web、ONVIF 或 NTP 定时器触发时间同步。
2. `time_service` 执行同步。
3. 成功后设置系统时间。
4. 发布 `TimeChanged`。
5. `time_service` 调用 `logger_service` 写操作审计。
6. OSD、录像、Web 状态模块收到通知。

## 14. 线程模型

建议线程：

```text
main-thread       初始化、启动、信号处理
http-thread       HTTP、WebSocket、API
net-io-thread     epoll 网络事件
rtsp-thread       RTSP 状态机和 RTP 发送
webrtc-thread     PeerConnection 和媒体发送
onvif-thread      SOAP、WS-Discovery
snapshot-thread   抓图请求
capture-thread    视频输入采集
encode-thread     编码帧获取
config-thread     配置写入和应用调度
event-thread      事件派发
time-thread       NTP 和周期校时
system-thread     heartbeat 和健康检查
log-thread        普通运行日志异步写入
logger-thread     用户操作审计异步写入（目标；当前 logger_service 仍同步写文件）
```

规则：

- 配置写入串行执行。
- session 表使用互斥锁或线程安全容器。
- 编码线程不阻塞等待网络发送。
- 事件回调不执行耗时操作。
- 锁内不调用外部 service 的复杂接口。
- 每个客户端发送队列必须有长度上限。
- `logger_service` 后续接入异步队列后，写 flash 或文件不能阻塞业务线程；当前同步文件后端仅作为最小实现。

## 15. 启动顺序

推荐启动顺序：

1. `infra_service`
2. `logger_service`
3. `config_service`
4. `event_service`
5. `auth_service`
6. `system_service`
7. `network_service`
8. `time_service`
9. `osd_service`
10. `media_service`
11. `rtsp_service`
12. `webrtc_service`
13. `snapshot_service`
14. `onvif_service`
15. `alarm_service`
16. `http_service`
17. `upgrade_service`

停止顺序与启动顺序相反。

## 16. 分阶段实现路线

### v0 工程骨架

目标：

- 所有 service 目录、public header、src、tests、module.mk、Makefile 创建完成。
- `infra_service` 提供 `Status`、`Result`、`IService`、`infra::Log` 最小实现。
- `logger_service` 提供 `ILoggerService`、`IOperationLogStore`、`FileOperationLogStore` 最小实现。
- `app/main.cpp` 能按顺序 Init、Start、Stop、Deinit。
- 程序能启动、打印运行日志、正常退出。

验收：

- `make all` 成功。
- `make -C libs/infra_service` 成功。
- `make -C libs/logger_service` 成功。
- 生成 `build/bin/live_stream`。
- 启动日志显示所有 service 生命周期。

### v1 基础服务

目标：

- `config_service` 支持配置读写和持久化。
- `event_service` 支持订阅和异步派发。
- `auth_service` 支持登录、登出、token 校验。
- `logger_service` 支持操作审计查询和导出。

验收：

- 登录、登出、认证失败能写操作审计。
- 修改配置能触发 `ConfigChanged`。
- 配置重启后仍可读取。
- `/api/operations` 能查询操作记录。

### v2 媒体假源和协议骨架

目标：

- `media_service` 提供 fake `FrameSource`。
- `FrameSource` 支持多个 `FrameSink`。
- `rtsp_service` 已具备私有 `IRtspFrameSource`/`IRtspFrameSink` 和 RTP 发送能力；下一步要接入 `media_service` fake source，并与项目统一 `IFrameSink` 语义对齐。
- `webrtc_service` 可接入假帧源。
- 慢 sink 不影响其他 sink。

验收：

- 多 sink 同时订阅同一码流。
- 一个慢 sink 丢帧不影响其他 sink。
- 关键帧请求能传回 `media_service`。
- RTSP TCP interleaved、UDP transport、Basic 鉴权和 adaptive observer 测试保持通过。

### v3 真实海思媒体

目标：

- `media_service` 接入海思 VI、VPSS、VENC。
- 输出主码流和子码流。
- 支持编码参数动态修改。
- OSD 能应用到媒体管线。

验收：

- 主码流、子码流稳定输出。
- 修改码率、帧率、GOP 后运行时生效。
- 媒体异常能发布事件并被 `system_service` 检测。

### v4 完整业务

目标：

- Web API 完整。
- RTSP 可用。
- WebRTC 浏览器可拉流。
- ONVIF 可发现、查询、返回 RTSP 和抓图 URL。
- 升级、重启、恢复出厂可用。

验收：

- Web 页面能登录和修改配置。
- VLC 或 ffplay 能播放 RTSP。
- 浏览器能 WebRTC 拉流。
- ONVIF 工具能发现设备。
- 所有关键用户操作都有审计记录。

## 17. 测试计划

功能测试：

- Web 页面访问。
- 登录、登出、token 过期。
- 配置读写、持久化、运行时应用。
- 用户操作审计记录、查询、导出。
- WebRTC 单客户端拉流。
- RTSP 单客户端拉流。
- ONVIF 发现和 `GetStreamUri`。
- 抓图返回 JPEG。
- 重启、恢复出厂、升级权限校验。

并发测试：

- 多 Web 客户端同时登录。
- 多 WebSocket 客户端订阅事件。
- 多客户端同时修改配置时串行处理。
- 多 RTSP/WebRTC 客户端共享同一 `FrameSource`。
- 慢 `FrameSink` 不影响编码线程和其他 sink。

稳定性测试：

- 长时间运行检查内存泄漏。
- 长时间运行检查线程泄漏。
- session 创建和释放无泄漏。
- 媒体线程异常后可恢复。
- 网络断开、NTP 失败、存储满时状态可观测。
- `logger_service` 当前需验证文件写失败能返回明确错误；后续异步化后再验证 flash 或文件写失败不阻塞业务线程。

安全测试：

- 未登录不能修改配置。
- 普通用户不能升级、重启、恢复出厂。
- RTSP 鉴权生效。
- ONVIF 鉴权生效。
- 普通运行日志和操作审计中不出现密码、token、密钥。

构建测试：

- `make all` 通过。
- `make test` 通过。
- `make -C libs/infra_service` 通过。
- `make -C libs/logger_service` 通过。
- `make -C libs/media_service` 通过。
- 每个静态库都能单独 clean、build、test。

## 18. AI 实现约束

AI 实现代码时必须遵守：

- 必须遵守第 3 章“软件设计硬性约束”。
- 先实现 v0，不允许跳过骨架直接写复杂业务。
- 所有 public API 放在对应 service 的 `include/`。
- 所有对外接口必须遵守接口与实现分离原则。
- public header 只允许出现接口、公共类型和工厂函数，不允许出现实现类。
- 实现类、内部状态、具体后端、平台适配和 HAL 必须放在 `src/`。
- 所有模块必须实现 `IService`。
- 所有 public API 返回 `Status` 或 `Result<T>`。
- 不使用异常。
- 不使用 RTTI。
- 不引入文档未指定的第三方库。
- 不直接访问其他模块 `src/`。
- 其他模块禁止 include 任意模块的 `src/` 内部头文件。
- 不直接调用平台 API，必须走 `infra_service`。
- 不直接调用海思 SDK，除非代码位于 `media_service` 内部 HAL。
- 不把媒体帧放入 `event_service`。
- 不让网络发送阻塞编码线程。
- 不记录敏感信息。
- 用户操作审计只能通过 `logger_service`，不能通过 `infra::Log` 代替。
- 流媒体生产者和消费者命名固定为 `FrameSource` 和 `FrameSink`。
- 每新增一个模块，必须同时提供基础测试或 header include 测试。

## 19. 默认假设

- 初期目标平台为 Linux/海思嵌入式 Linux。
- 初期编译为静态库。
- 初期 WebRTC 使用成熟库封装，具体库后续在 `third_party` 决定。
- 初期抓图复用主码流或子码流。
- 初期配置文件使用 JSON。
- 初期普通运行日志使用 `infra::Log` 文件输出和控制台输出。
- 初期用户操作审计使用 `logger_service` 的 `FileOperationLogStore`，格式为 JSON Lines。
- 用户操作审计存储路径可位于 flash 文件系统，例如 `/mnt/flash/logs/operation.log`。
- 后续如需直接写 flash 分区，只新增 `FlashRingOperationLogStore`，不修改业务 service。

## 20. 当前模块设计与实现 View

本章记录当前源码与前文目标设计之间的对应关系、偏差和优化方向。前文第 1-19 章仍作为目标架构和约束，本章作为当前阶段评审视图；当源码和旧分析文档冲突时，以当前源码为准。

### 20.1 当前实现分层

| 分组 | 模块 | 当前状态 | 说明 |
| --- | --- | --- | --- |
| 基础能力较完整 | `infra_service` | 已实现并有多项单测 | 提供 `Status`、`Result`、`IService`、文件、路径、日志、线程、队列、线程池、定时器、基础同步和媒体 buffer。 |
| 已接入 app registry | `logger_service`、`config_service`、`event_service`、`auth_service`、`netframe_service`、`time_service` | 主程序真实 Init/Start/Stop/Deinit | `app/main.cpp` 已建立最小 service registry；auth 审计通过 adapter 写入 `logger_service`；未接入模块以 pending 日志显式标记。 |
| 业务基础较完整 | `auth_service` | 已有 public API、实现和测试 | 提供用户存储、密码校验、token session、权限检查和审计 sink；app 已接入 logger 审计 adapter，仍需生产级凭据能力。 |
| 业务基础较完整 | `config_service` | 已有 public API、实现和测试 | 当前是配置名路径、JSON store、verify/apply/notify、converge/diverge 和延迟保存模型。 |
| 业务基础较完整 | `event_service` | 已有 public API、实现和测试 | 支持轻量状态事件订阅、取消订阅和异步发布；禁止传媒体帧的边界与目标设计一致。 |
| 业务基础较完整 | `logger_service` | 已有 public API、实现和测试 | 提供操作审计写入、查询、导出、JSON Lines 编解码和文件轮转。 |
| 业务基础较完整 | `netframe_service` | 已有 public API、实现和网络测试 | 提供 event loop、TCP server、UDP socket、reactor、统计和慢客户端处理。 |
| 业务入口骨架 | `http_service`、`rtsp_service` | 已有接口、工厂、依赖注入和测试 | 已依赖 `auth/config/logger/netframe/event/media` 等接口；HTTP 支持 request/TCP 分片测试、媒体能力查询和视频配置能力校验，RTSP 已有会话、RTP 发送和慢客户端统计骨架；尚未接入 app registry 和完整业务资源。 |
| 设备控制骨架 | `system_service`、`time_service`、`alarm_service` | 已有 `I<Service>`、工厂和基础测试 | `system_service` 通过平台接口提供设备信息、状态、能力、重启/恢复出厂和 heartbeat；`time_service` 支持平台校时、事件和审计；`alarm_service` 支持规则、输入注入和状态清除。 |
| 媒体侧骨架 | `media_service`、`osd_service`、`snapshot_service` | 已实现 concrete `infra::IService` 和基础测试 | 已有 MPP channel、媒体能力输出、OSD region、snapshot capture 的骨架逻辑；仍未完成 `I<Service>`/工厂分离、真实 HAL、FrameSource/FrameSink。 |
| 占位骨架 | `network_service`、`webrtc_service`、`onvif_service`、`upgrade_service` | 仍是 `Name()` 占位类和 header include 测试 | 尚未实现 `infra::IService`、接口/实现分离、工厂函数和业务能力。 |

### 20.2 模块级设计 View

| 模块 | 当前 public API | 当前实现状态 | 测试状态 | 主要冲突或偏差 | 优化建议 |
| --- | --- | --- | --- | --- | --- |
| `infra_service` | 多个 `infra/*` public header，包含 `Status`、`Result`、`IService`、文件、日志、线程、队列、定时器等。 | 基础设施已拆成多个 `src/` 实现。 | 有 error/result/header/file/log/media_buffer/thread_pool/timer 等测试。 | 前文禁止业务模块直接调用平台 API；`infra_service` 自身作为平台封装层可以调用平台 API。 | 保持 public API 中文注释标准；后续补 durable file write、平台适配目录和更细错误映射。 |
| `auth_service` | `IAuthService`、`IAuthUserStore`、`IPasswordVerifier`、`IAuthAuditSink`、登录/token/权限类型。 | 已实现内存用户、JSON 用户存储、明文和 SHA-256 校验、session 管理和权限检查。 | 有登录、权限、token 过期、JSON 用户存储等测试。 | 手写 SHA-256 和明文校验器只适合骨架/开发阶段；审计通过 sink 隔离，未直接接入 `logger_service`。 | 生产阶段替换为受控密码哈希方案；统一 auth/http/onvif 的权限前置检查；补充 token 撤销、锁定和审计适配。 |
| `config_service` | `IConfigService` 提供 `SetConfig`、`GetConfig`、`SaveFile`、`AttachApply`、`AttachVerify`、`AttachNotify`、`AttachConverge`、`AttachDiverge`。 | 已实现 JSON 解析/写回、配置路径解析、内存 store、临时文件 rename 持久化和回调注册。 | 有配置读写、持久化、回调、延迟保存等测试。 | 当前源码不是前文描述的 schema/version/audit/event 模型；没有统一 `ConfigChanged` 事件发布，也没有直接写 `logger_service` 审计。`libs/config_service/analysis` 中旧文档也描述了已过期接口。 | 选择并固化一种配置模型：要么更新前文第 9/13.3 章为当前 callback 模型，要么重构源码回 schema/version 模型；补权限、字段级 schema、事件/审计适配、fsync/CRC/备份恢复。 |
| `event_service` | `IEventService`、`EventType`、`Event`、`EventHandler`、订阅 ID。 | 使用 `infra::TaskQueue` 异步派发事件，订阅表有 128 个上限，事件字段有长度限制。 | `make -C libs/event_service test` 通过，覆盖 lifecycle、订阅发布、取消订阅、上限、字段长度和错误路径。 | 事件类型已经列出多个业务事件，但具体业务模块多数仍未接入。 | 后续定义 `ConfigChanged`、媒体状态、告警状态的字段约定；保持事件不传大 JSON、二进制和媒体帧。 |
| `logger_service` | `ILoggerService`、`LoggerServiceConfig`、`OperationRecord`、查询和导出接口。 | 已实现 `LoggerServiceImpl`、内部 `IOperationLogStore`、`FileOperationLogStore`、JSON Lines 编解码、查询、导出和轮转。 | `make -C libs/logger_service test` 通过，覆盖 lifecycle、写入、查询、导出和 stopped 后拒绝写入。 | 当前 `RecordOperation()` 同步写文件，`queue_capacity` 尚未接入异步队列；写 flash 或慢文件系统时可能阻塞调用线程。 | 后续接入有界异步队列、丢失计数和 `infra::Log` 错误上报；敏感字段仍由调用方保证不传入。 |
| `netframe_service` | `INetframeService`、`IEventLoop`、`ITcpServer`、`ITcpConnection`、`IUdpSocket`、`IReactor`，以及 loop/TCP/UDP stats 类型。 | 已有 epoll loop、wakeup pipe、timer map、TCP/UDP、发送队列、慢客户端判断、stats 和 reactor 代码。 | `make -C libs/netframe_service test` 通过，覆盖 event loop、reactor、TCP/UDP loopback、发送队列、发送 rollback 和慢客户端。 | Linux epoll/socket 实现仍集中在单个 `src/netframe_service.cpp`，平台边界还不够清晰。 | 后续将 Linux 相关实现收敛到 `src/platform/linux` 或内部 adapter，并补更多关闭时回调安全测试。 |
| `time_service` | `ITimeService`、`ITimePlatform`、`TimeStatus`、`NtpConfig`、`CreateTimeService()`。 | 已实现 platform-adapter service；支持 timezone、manual set time、NTP sync、`EventType::kTimeChanged` 发布和 `logger_service` 审计。 | `make -C libs/time_service test` 通过，覆盖生命周期、状态查询、设置时区、手动校时和 NTP sync fake platform。 | 当前 app 以无平台模式启动，只验证生命周期；没有后台 NTP 定时器，也不直接调用 Linux `clock_settime`。 | 后续补 Linux 平台实现、周期性 NTP、配置持久化和 app 依赖注入。 |
| `system_service` | `ISystemService`、`ISystemPlatform`、`SystemServiceOptions`、`CreateSystemService()`。 | 已实现平台接口转发、heartbeat 健康检查、系统状态事件发布和重启/恢复出厂审计。 | `make -C libs/system_service test` 通过，使用 fake platform 覆盖生命周期、设备信息、状态、能力、heartbeat、重启和恢复出厂。 | 依赖真实平台实现；尚未接入 app registry，且重启/恢复出厂权限仍需要入口模块保障。 | 后续补 Linux/海思平台 adapter、权限矩阵和 app 接入策略。 |
| `media_service` | `MediaService` concrete class、`MediaPipelineConfig`、`MediaChannels`、`MediaCapabilities`、`SetEncodedFrameCallback()`、channel 查询。 | 已实现生命周期状态机、配置校验、MPP channel 元数据构建和 host/mock 媒体能力输出；真实海思 HAL 能力查询尚未接入。 | `make -C libs/media_service test` 通过，覆盖 Init 前/后 Channels 状态和能力输出。 | 仍不是第 11 章目标 `IMediaService` 接口；缺少 FrameSource/FrameSink 订阅、关键帧请求、编码参数运行时配置和真实 HiSDK 能力枚举。 | P1：在保持现有 MPP channel API 可用的前提下，引入 `IMediaService`/工厂、fake FrameSource、FrameSink 订阅和关键帧请求测试；将 `MppAdapter::GetCapabilities()` 替换为真机 HiSDK/MPP 查询并建立缓存刷新策略。 |
| `osd_service` | `OsdService` concrete class、OSD region 配置、`BindMedia()`、region create/attach/visible/update/destroy。 | 已实现 OSD region 生命周期骨架和媒体 channel 绑定。 | `make -C libs/osd_service test` 通过，覆盖绑定媒体、创建区域、attach、visible、destroy。 | 仍未接口/工厂分离，未接真实 RGN/VPSS/VENC HAL，也未接配置和审计。 | P2：补 `IOsdService`/工厂，内部再封装海思 RGN adapter；与 media channel 生命周期建立失效处理。 |
| `snapshot_service` | `SnapshotService` concrete class、`SnapshotConfig`、`BindMedia()`、`Capture()`。 | 已实现 snapshot capture 会话骨架、媒体 channel 绑定和 JPEG frame 元数据返回。 | `make -C libs/snapshot_service test` 通过，测试链接 `media_service` 并覆盖 capture 主路径。 | 仍未接口/工厂分离，未接真实 snap pipe/JPEG VENC，也未复用 FrameSource。 | P2：补 `ISnapshotService`/工厂，明确抓图走 MPP snap pipe 还是 FrameSource 缓存，并补超时、并发和错误恢复测试。 |
| `http_service` | `IHttpService`、HTTP request/response/options/dependencies/stats、`CreateHttpService()`，依赖可注入 `MediaService`。 | 已实现依赖注入、request id、Bearer token、JSON response、静态文件保护、TCP fragment 处理、`GET /api/media/capabilities` 和 `PUT /api/config/video` 能力校验。 | `make -C libs/http_service test` 通过，覆盖 header/request/TCP fragment、媒体能力接口和不支持视频参数拒绝。 | 尚未接入 app registry；REST 资源只落地部分骨架，WebSocket、WebRTC signaling 和前端托管仍需补齐；当前临时依赖 concrete `MediaService`，后续应切到 `IMediaService`。 | P1：在 app 中按真实依赖接入 HTTP，并补 REST 路由表、权限矩阵、WebSocket 状态推送和静态资源路径策略；`media_service` 接口化后把 HTTP 依赖改为 `IMediaService*`。 |
| `rtsp_service` | `IRtspService`、`IRtspFrameSource`、`IRtspFrameSink`、`IRtspAdaptiveObserver`、`CreateRtspService()`，同时保留 legacy `RtspService::Name()`。 | 已实现 RTSP server、session 状态机、Basic 鉴权、TCP interleaved/UDP transport、H.264/H.265 RTP 分包、首个关键帧门控、stats、事件发布和 adaptive observer 通知。 | `make -C libs/rtsp_service test` 通过，覆盖 header、TCP interleaved、UDP transport、Basic 鉴权和 adaptive observer。 | 尚未接入 `app/main.cpp`；尚未统一到 `media_service` 最终 `IMediaService`/`IFrameSink`；缺少 RTCP、session timeout、Digest 鉴权、H.265 SDP 和自适应动作执行。 | P1：先接入 app registry 和 media fake source；补 DESCRIBE/SETUP/PLAY 错误路径、慢客户端、分片边界、H.265 SDP/RTCP 和 session timeout 测试。 |
| `alarm_service` | `IAlarmService`、`AlarmRule`、`AlarmInput`、`AlarmStatus`、`CreateAlarmService()`。 | 已实现规则更新、规则使能、告警输入注入、min duration、状态查询、清除和 `EventType::kAlarmTriggered` 发布。 | `make -C libs/alarm_service test` 通过，覆盖规则、触发、状态和清除。 | 目前未接配置、审计和真实平台输入源，min duration/debounce 测试仍偏薄。 | P2：接入配置、审计和平台输入源，补 min duration/debounce 边界测试。 |
| `webrtc_service`、`onvif_service` | 当前仍为 `Name()` 占位。 | 占位。 | header include 测试。 | 尚未通过 `media_service` public interface 获取帧；WebRTC 第三方库也未接入。 | P2：等待 `media_service` fake source 稳定后再实现协议骨架；WebRTC 只能封装成熟库。 |
| `network_service`、`upgrade_service` | 当前仍为 `Name()` 占位。 | 占位。 | header include 测试。 | 未实现 `infra::IService`、工厂函数、配置接入、审计和事件发布。 | P2/P3：补接口、平台 adapter、生命周期测试、权限和回滚路径。 |

### 20.3 关键冲突清单

| 编号 | 严重级别 | 冲突点 | 影响 | 建议处理 |
| --- | --- | --- | --- | --- |
| C1 | P0 | app registry 只接入了基础 service，媒体、HTTP 和设备控制模块仍未接入。 | 主程序能验证基础生命周期，但还不能启动完整 IPC 业务路径。 | 下一步优先接入 `http_service`；media/osd/snapshot 需先补工厂或 app adapter。 |
| C2 | P0 | `network/webrtc/onvif/upgrade` 仍是 `Name()` 占位类，没有实现 `infra::IService`。 | 与第 3.3、3.4、18 章硬性约束冲突。 | 后续新增模块能力前，先统一改为 `I<Service>` + `Create<Service>` + 生命周期测试。 |
| C3 | P1 | `config_service` 当前 callback/path 模型与前文 schema/version/audit/event 模型不一致。 | 文档读者会误判配置中心能力；旧 `analysis` 文档也已过期。 | 先决策配置模型，再同步第 9、13.3、20 章和 `libs/config_service/analysis`。 |
| C4 | P1 | `config_service` 配置成功后没有统一发布 `event_service::kConfigChanged`，也没有统一写 `logger_service`。 | 前文配置修改流程第 7、8 步尚未落地。 | 在 logger/event 稳定后增加适配层，或在入口 service 侧明确承担审计与通知。 |
| C5 | P1 | `netframe_service` 是网络平台适配边界，但源码目录未显式区分 Linux 实现。 | 后续移植或业务模块引用平台 API 时容易混淆边界。 | 在 P0 编译修复后，将 Linux epoll/socket 实现收敛到内部 platform 目录，并在 public header 保持平台无关。 |
| C6 | P2 | `auth_service` 有开发用明文密码校验器和手写 SHA-256。 | 生产安全能力不足，容易被误用。 | 文档和代码注释保留“仅开发/测试”边界；生产阶段接入受控密码哈希和随机数能力。 |
| C7 | P2 | 当前 `config_service` 持久化使用 tmp rename，但缺少 fsync、CRC、主备和迁移。 | 掉电或升级时配置恢复可靠性不足。 | 增加 durable write、schema version、迁移表和损坏文件恢复策略。 |
| C8 | P1 | `media/osd/snapshot` 已经有 concrete service，但 public header 仍暴露实现类而不是 `I<Service>`/工厂。 | 与第 18 章“public header 只暴露接口和工厂”的目标不一致，后续 app registry 和依赖替换困难。 | 先补兼容工厂和接口，再逐步把具体实现类移入 `src/` 或保持为 legacy wrapper。 |
| C9 | P1 | `time/system` 已有接口和事件/审计钩子，但尚未接真实 Linux 平台和 app 依赖注入。 | 当前只能做单测/fake platform 或无平台生命周期验证，不能满足设备控制闭环。 | 增加 Linux platform adapter、NTP 定时器、配置持久化、权限校验和 app 接入。 |
| C10 | P1 | Web 视频配置已经改为能力驱动，但当前能力来自 host/mock adapter，尚未从真实海思 SDK 查询。 | 开发环境可验证接口闭环，真机上仍可能出现 UI 可选项与实际 VENC 能力不一致。 | 在 `media_service` 内部实现 HiSDK/MPP 能力 adapter，并补真机能力 dump、缓存刷新和不支持组合的错误映射测试。 |

### 20.4 优先级路线

短期 P0：

- 保持 app 最小 service registry 可干净构建并真实启动 `logger/config/event/auth/netframe/time`。
- 将 `network/webrtc/onvif/upgrade` 的 public header 升级为 `I<Service>`、`Create<Service>` 和 `infra::IService` 生命周期骨架。
- 为 `media/osd/snapshot` 补接口/工厂兼容层，减少 concrete class 暴露带来的依赖耦合。
- 清理或更新 `libs/config_service/analysis` 中与当前源码不一致的旧设计描述。

中期 P1：

- 决策 `config_service` 最终模型，并同步第 9 章和第 13.3 章。若保留当前 callback/path 模型，需要补字段 schema、权限、事件和审计适配；若回到 schema/version 模型，需要重构当前源码和测试。
- 为 `media_service` 建立 fake `FrameSource`、`FrameSink`、`EncodedFrame` 订阅和关键帧请求链路，供 RTSP、WebRTC、Snapshot、ONVIF 后续接入。
- 将 `http_service` 接入 app registry，补完整 REST 路由表、权限矩阵、WebSocket 状态推送和静态前端目录。
- 将 Web 视频配置保持为能力驱动：前端只使用 `/api/media/capabilities` 生成可选项，HTTP 保存时继续以后端能力校验为准；真机阶段由 `media_service` 的 HiSDK adapter 替换 host/mock 能力。
- 为 `time_service` 增加真实平台 adapter、周期性 NTP、配置持久化和 app 依赖注入。
- 将 `netframe_service` Linux 细节收敛为内部平台适配，补关闭时回调安全测试。

中长期 P2/P3：

- 基于稳定的 `auth/config/event/logger/media/netframe` 接口推进 HTTP、RTSP、WebRTC、ONVIF、Snapshot。
- 补 network、upgrade 的配置、审计和事件路径，完善 system/alarm 的真实平台输入和权限路径。
- 为 flash 写入、升级、恢复出厂、校时和网络修改补充掉电恢复、权限、审计和失败回滚测试。
