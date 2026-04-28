/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: request_context.h
 * Brief: 定义跨 service 传递的请求上下文信息。
 */

#ifndef LIVE_STREAM_INFRA_REQUEST_CONTEXT_H_
#define LIVE_STREAM_INFRA_REQUEST_CONTEXT_H_

#include <string>

namespace infra {

/**
 * @brief 请求上下文公共结构。
 *
 * 作用：
 * - 在 HTTP、鉴权、配置、系统控制等 service 之间传递请求来源信息。
 * - 供运行日志、操作审计、权限判断等模块引用。
 *
 * 安全约束：
 * - 字段只能保存标识符和来源信息。
 * - 禁止保存密码、token、密钥、认证头等敏感明文。
 */
struct RequestContext {
    std::string request_id;  ///< 请求唯一标识，由入口模块生成或透传。
    std::string user_name;   ///< 当前认证用户名称，未认证时可为空。
    std::string session_id;  ///< 当前会话标识，未建立会话时可为空。
    std::string client_ip;   ///< 客户端 IP 地址字符串。
    std::string user_agent;  ///< 客户端 User-Agent 或设备标识，可为空。
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_REQUEST_CONTEXT_H_
