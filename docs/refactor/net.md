优化计划
  明确 libs/net 只提供这些能力：EventLoop、Timer、TcpServer、TcpConnection、TcpClient，以及必要的 SocketUtil。不要把 HTTP、鉴权、媒体业务、配置逻辑放进 libs/net。

  第二阶段：收敛 fd 生命周期
  制定硬规则：fd 归属一个连接对象；事件回调只在 loop 线程执行；关闭流程统一为 DisableEvents -> RemoveFromPoller -> CloseFd -> NotifyClosed。避免上层直接 close fd。

  第三阶段：补齐发送队列和背压
  每个连接维护有上限的发送队列。Send() 只入队或尝试立即发送；EAGAIN 后监听写事件；队列超过上限时返回失败或主动断开慢客户端。增加 IsWriteBlocked() 或等价接口供 HTTP/流输出做判断。

  第四阶段：统一错误语义
  定义项目自己的 NetErrorCode，至少包含：kOk、kEof、kTimeout、kConnectionReset、kConnectionRefused、kDnsFailed、kClosedByLocal、kSystemError。不要把 errno 字符串散落到业务层。

  第五阶段：明确线程模型
  所有连接 IO 在所属 EventLoop 执行；跨线程只允许 PostTask()。公开接口注释写清楚是否线程安全。不要为了“方便”让 socket 被多个线程直接操作。

  第六阶段：timer 和连接管理合并到 loop
  idle timeout、connect timeout、send timeout 都由 loop 内 timer 驱动，避免业务线程直接取消/释放网络对象造成竞态。

  第七阶段：按板端做资源上限
  配置并默认限制：最大连接数、单连接读缓冲、单连接写队列、全局写队列、accept backlog、HTTP keep-alive 秒数、慢客户端发送超时。

  第八阶段：验证
  至少补这些验证用例：半包读取、连续多包读取、客户端断开、服务端主动关闭、发送 EAGAIN、写队列超限、idle timeout、重复 close、连接回调中关闭自身、短连接压测、慢客户端压测。

  建议路线

  保留 libs/net，不要直接替换成 ZLToolKit。
  从 ZLToolKit 借鉴的是“事件循环 + fd 生命周期 + 发送队列 + 超时 + session 边界”的成熟语义，而不是照搬它的通用库形态。对本项目来说，最优目标是一个小而硬的网络基础层：接口少、资源有
  上限、线程归属清楚、失败路径可追踪。

