/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: log.h
 * Brief: 定义普通运行日志接口和 INFRA_LOG_* 便捷宏。
 */

#ifndef LIVE_STREAM_INFRA_LOG_H_
#define LIVE_STREAM_INFRA_LOG_H_

#include <cstdint>
#include <string>

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
    LogLevel min_level = LogLevel::kInfo;
    std::string file_path;
    uint32_t max_file_size_kb = 0;
    uint32_t max_file_count = 0;
    bool console_output = true;
    bool async_write = false;
};

/**
 * @brief 普通运行日志全局接口。
 *
 * 作用：
 * - 输出诊断、调试和运行状态日志。
 * - 支持等级过滤、模块名、源文件行号、控制台输出、文件输出和可选异步写入。
 *
 * 安全边界：
 * - 禁止记录密码、token、密钥、认证头等敏感明文。
 * - 禁止用该接口代替用户操作审计；登录、配置修改、升级、重启等操作必须走 logger_service。
 *
 * 线程安全：
 * - Init()、Shutdown()、Write() 可被多线程调用。
 * - 建议由 app 在启动早期 Init()，在退出末尾 Shutdown()。
 */
class Log {
 public:
    /**
     * @brief 初始化全局日志运行时。
     *
     * @param config 日志配置，包含等级、输出路径、轮转和异步写入设置。
     *
     * @return 成功返回 true；文件路径无法打开时返回 false。
     *
     * @note 重复调用 Init() 会先关闭旧运行时，再使用新配置初始化。
     */
    static bool Init(const LogConfig& config);

    /**
     * @brief 关闭全局日志运行时。
     *
     * 作用：
     * - 刷新异步队列中的日志。
     * - 停止异步写线程。
     * - 关闭日志文件。
     *
     * @note 可重复调用；未 Init() 时调用也应安全返回。
     */
    static void Shutdown();

    /**
     * @brief 写入一条格式化运行日志。
     *
     * @param level 日志等级。
     * @param module 模块名，允许为空；为空时实现应使用 "unknown"。
     * @param file 源文件名，通常由 INFRA_LOG_* 宏传入 __FILE__。
     * @param line 源文件行号，通常由 INFRA_LOG_* 宏传入 __LINE__。
     * @param fmt printf 风格格式字符串，允许为空。
     * @param ... fmt 对应的可变参数。
     *
     * @note 一般业务代码应使用 INFRA_LOG_* 宏，不直接调用 Write()。
     */
    static void Write(LogLevel level,
                      const char* module,
                      const char* file,
                      int line,
                      const char* fmt,
                      ...);
};

}  // namespace infra

#define INFRA_LOG_TRACE(module, fmt, ...) \
    infra::Log::Write(infra::LogLevel::kTrace, module, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define INFRA_LOG_DEBUG(module, fmt, ...) \
    infra::Log::Write(infra::LogLevel::kDebug, module, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define INFRA_LOG_INFO(module, fmt, ...) \
    infra::Log::Write(infra::LogLevel::kInfo, module, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define INFRA_LOG_WARN(module, fmt, ...) \
    infra::Log::Write(infra::LogLevel::kWarn, module, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define INFRA_LOG_ERROR(module, fmt, ...) \
    infra::Log::Write(infra::LogLevel::kError, module, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define INFRA_LOG_FATAL(module, fmt, ...) \
    infra::Log::Write(infra::LogLevel::kFatal, module, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif  // LIVE_STREAM_INFRA_LOG_H_
