/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: service.h
 * Brief: 定义所有 service 统一遵守的生命周期接口。
 */

#ifndef LIVE_STREAM_INFRA_SERVICE_H_
#define LIVE_STREAM_INFRA_SERVICE_H_

#include "infra/status.h"

#include <mutex>

namespace infra {

/**
 * @brief service 生命周期基础接口。
 *
 * 生命周期顺序：
 * - 构造函数：只做轻量成员初始化，禁止启动线程、打开设备、监听端口。
 * - Init()：准备资源、检查依赖、加载必要状态。
 * - Start()：启动线程、定时器、监听、媒体管线等运行态资源。
 * - Stop()：停止接收新任务，等待线程和回调退出。
 * - Deinit()：释放 Init() 阶段申请的资源。
 *
 * 约束：
 * - Stop() 和 Deinit() 必须支持重复调用或安全忽略重复调用。
 * - 析构函数只能做兜底清理，不能依赖析构完成正常停机流程。
 */
class IService {
 public:
    virtual ~IService() = default;

    /**
     * @brief 初始化 service 资源。
     *
     * @return 成功返回 kOk；依赖缺失、参数非法或资源申请失败时返回对应错误码。
     */
    virtual Status Init() = 0;

    /**
     * @brief 启动 service 运行态逻辑。
     *
     * @return 成功返回 kOk；重复启动可返回 kOk 或 kBusy，具体由实现说明。
     */
    virtual Status Start() = 0;

    /**
     * @brief 停止 service 运行态逻辑。
     *
     * @note 该函数应阻止新任务进入，并等待内部线程、定时器、回调退出到安全状态。
     */
    virtual void Stop() = 0;

    /**
     * @brief 释放 Init() 阶段申请的资源。
     */
    virtual void Deinit() = 0;

    /**
     * @brief 获取 service 诊断名称。
     *
     * @return 返回静态或对象生命周期内有效的字符串指针，调用方不能释放。
     */
    virtual const char* Name() const = 0;
};

enum class ServiceState {
    kCreated,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

class ServiceBase {
 public:
    virtual ~ServiceBase() = default;

    Status Init() {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (state_ == ServiceState::kInitialized ||
            state_ == ServiceState::kStarted ||
            state_ == ServiceState::kStopped) {
            return Status::kOk;
        }
        if (state_ != ServiceState::kCreated &&
            state_ != ServiceState::kDeinitialized) {
            return Status::kInternalError;
        }
        const Status status = OnInit();
        if (status == Status::kOk) {
            state_ = ServiceState::kInitialized;
        }
        return status;
    }

    Status Start() {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (state_ == ServiceState::kStarted) {
            return Status::kOk;
        }
        if (state_ != ServiceState::kInitialized &&
            state_ != ServiceState::kStopped) {
            return Status::kInternalError;
        }
        const Status status = OnStart();
        if (status == Status::kOk) {
            state_ = ServiceState::kStarted;
        }
        return status;
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (state_ != ServiceState::kStarted) {
            return;
        }
        OnStop();
        state_ = ServiceState::kStopped;
    }

    void Deinit() {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (state_ == ServiceState::kStarted) {
            OnStop();
            state_ = ServiceState::kStopped;
        }
        if (state_ == ServiceState::kInitialized ||
            state_ == ServiceState::kStopped) {
            OnDeinit();
        }
        state_ = ServiceState::kDeinitialized;
    }

 protected:
    ServiceBase() = default;

    bool IsInitializedForRead() const {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        return state_ == ServiceState::kInitialized ||
               state_ == ServiceState::kStarted ||
               state_ == ServiceState::kStopped;
    }

    bool IsStartedForRead() const {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        return state_ == ServiceState::kStarted;
    }

    ServiceState state_for_test() const {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        return state_;
    }

    virtual Status OnInit() { return Status::kOk; }
    virtual Status OnStart() { return Status::kOk; }
    virtual void OnStop() {}
    virtual void OnDeinit() {}

 private:
    mutable std::mutex lifecycle_mutex_;
    ServiceState state_ = ServiceState::kCreated;
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_SERVICE_H_
