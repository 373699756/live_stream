/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: string_util.h
 * Brief: 定义无业务语义的字符串基础工具函数。
 */

#ifndef LIVE_STREAM_INFRA_STRING_UTIL_H_
#define LIVE_STREAM_INFRA_STRING_UTIL_H_

#include <string>
#include <vector>

namespace infra {

/**
 * @brief 字符串工具接口。
 *
 * 作用：
 * - 提供基础字符串裁剪、拆分、前缀和后缀判断。
 * - 只处理通用字符串逻辑，不加入配置、协议、媒体等业务语义。
 *
 * 线程安全：
 * - 所有函数无共享状态，可被多线程并发调用。
 */
class StringUtil {
 public:
    /**
     * @brief 去除字符串首尾空白字符。
     *
     * @param input 输入字符串。
     *
     * @return 返回去除首尾 ASCII 空白字符后的新字符串。
     */
    static std::string Trim(const std::string& input);

    /**
     * @brief 按单字符分隔符拆分字符串。
     *
     * @param input 输入字符串。
     * @param delimiter 分隔符字符。
     *
     * @return 返回拆分结果；连续分隔符和末尾分隔符会保留空字段。
     */
    static std::vector<std::string> Split(const std::string& input, char delimiter);

    /**
     * @brief 判断字符串是否以指定前缀开头。
     *
     * @param input 输入字符串。
     * @param prefix 前缀字符串，可为空；空前缀返回 true。
     *
     * @return 匹配返回 true，否则返回 false。
     */
    static bool StartsWith(const std::string& input, const std::string& prefix);

    /**
     * @brief 判断字符串是否以指定后缀结尾。
     *
     * @param input 输入字符串。
     * @param suffix 后缀字符串，可为空；空后缀返回 true。
     *
     * @return 匹配返回 true，否则返回 false。
     */
    static bool EndsWith(const std::string& input, const std::string& suffix);
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_STRING_UTIL_H_
