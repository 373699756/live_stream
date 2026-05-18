 内存效率优化计划：减少拷贝、替换 vector 滥用

 Context

 本项目是 HiSilicon 平台上的实时视频流服务器，核心路径：
 硬件编码器输出 → EncodedFrame（IMediaBuffer + shared_ptr）→ StreamHubService → HLS/FLV 分发。

 整体框架的内存设计已经很好（IMediaBufferPool + shared_ptr 零拷贝传帧），
 但在 转码打包层（stream_mux、stream_hub_stream_state）存在大量低效点：

 1. std::string 当字节容器：TS segment body、FLV tag、PES packet 均用 std::string，每次 append/+ 触发 realloc +
 memcpy，且无法 reserve 准确大小。
 2. 临时 std::vector<uint8_t> fragment：PacketizeH264 / PacketizeH265 每分片循环内 new 一个 vector，然后再 insert 进 RTP
 packet bytes，两次拷贝。
 3. BuildPesPacket / BuildPatPacket 等返回 std::string：多级函数调用链产生多次值语义拷贝，最终
 segment_body->append(packet)。
 4. ParsedFramePayload 含 vector<H264NalUnit> / vector<H265NalUnit>：每帧都在堆上重新分配，频率等于帧率（25~60 fps）。
 5. PackagedFrameResult / ProcessFrame 中 std::string 出参：sequence_header_tag、flv_tag 等通过值返回，跨锁边界拷贝。
 6. AppendBytes 逐字节循环：stream_mux.cpp:94 用 push_back 替代 memcpy。

 ---
 关键文件

 ┌─────────────────────────────────────────────────────────┬────────────────────────────────────────────────────────┐
 │                          文件                           │                          问题                          │
 ├─────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┤
 │ libs/stream_mux/src/stream_mux.cpp                      │ 主要优化目标：临时 string/vector、AppendBytes、RTP分片 │
 ├─────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┤
 │ libs/stream_mux/include/stream_mux.h                    │ RtpPacket 结构、公开函数签名                           │
 ├─────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┤
 │ libs/stream_hub_service/src/stream_hub_stream_state.h   │ ParsedFramePayload、HlsSegmentState::body              │
 ├─────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┤
 │ libs/stream_hub_service/src/stream_hub_stream_state.cpp │ AppendFrameToStream、BuildH264/H265Outputs             │
 ├─────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┤
 │ libs/stream_hub_service/src/stream_hub_service.cpp      │ ProcessFrame 锁边界 string 拷贝                        │
 └─────────────────────────────────────────────────────────┴────────────────────────────────────────────────────────┘

 ---
 优化方案（分优先级）

 P0：消除热路径内每帧临时分配

 1. AppendBytes 替换为 memcpy 或 string::append

 文件：stream_mux.cpp:94–98

 // 改前（逐字节）
 void AppendBytes(std::string *out, const uint8_t *data, size_t size) {
   for (size_t i = 0; i < size; ++i) { AppendByte(out, data[i]); }
 }

 // 改后
 void AppendBytes(std::string *out, const uint8_t *data, size_t size) {
   out->append(reinterpret_cast<const char *>(data), size);
 }

 2. RTP 分片消除 vector<uint8_t> fragment 临时对象

 文件：stream_mux.cpp:PacketizeH264 (622–639), PacketizeH265 (658–676)

 当前在每个循环 iteration 中 new 一个 vector，再调 SendRtpPacket 拷贝进去。

 改法：直接在 SendRtpPacket 内部 reserve 目标空间后手动写 FU header + insert NAL 数据，
 或给 SendRtpPacket 增加 header-bytes + payload 分段参数，内部 reserve(header_size + size + 16) 一次写入，
 彻底消除中间 fragment vector：

 // 新签名（不暴露到头文件）
 void SendRtpPacket(const EncodedFrame &frame,
                    const uint8_t *header, uint32_t header_size,
                    const uint8_t *payload, uint32_t payload_size,
                    bool marker, uint16_t *sequence, uint32_t ssrc,
                    std::vector<RtpPacket> *packets);

 PacketizeH264/H265 内部直接拼 fu_indicator + fu_header 成 2/3 字节 header 数组传入。

 3. BuildPesPacket / BuildPatPacket 改为写入输出参数而非返回值

 文件：stream_mux.cpp:327–344, 257–282, 284–312

 // 改前
 std::string BuildPesPacket(const std::string &access_unit, ...);

 // 改后：直接追加到 segment_body，省去中间 string
 void AppendPesPacket(const std::string &access_unit,
                      uint64_t pts_90k, uint64_t dts_90k,
                      std::string *out);
 // AppendTsPayload 也随之去掉中间层

 AppendVideoAccessUnitToTsSegment 内部直接 pes 写入 segment_body，省去 pes string 的生命周期。

 4. HlsSegmentState::body 预留空间

 文件：stream_hub_stream_state.cpp:StartSegment

 每路流在 StartSegment 时 body.reserve(estimated_segment_bytes)（按帧率×帧大小×segment时长估算，
 可从 options_.hls_segment_duration_ms 和历史帧大小推算）：

 void StartSegment(StreamContext *stream, int64_t pts_us) {
   stream->current_segment = HlsSegmentState{};
   // 按 3s 段、4Mbps = ~1.5MB 预估
   stream->current_segment.body.reserve(2 * 1024 * 1024);
   ...
 }

 ---
 P1：减少跨锁边界的 string 拷贝

 5. PackagedFrameResult 改用 shared_ptr<string> 或 move

 文件：stream_hub_stream_state.h:50–55, stream_hub_service.cpp:347–348

 struct PackagedFrameResult {
   bool accepted = false;
   bool hls_segment_created = false;
   std::string sequence_header_tag;  // 保持，因为已是 move 语义
   std::string flv_tag;              // 同上
 };

 AppendFrameToStream 返回时 result.flv_tag = std::move(...) 已是现状，
 问题在于 ProcessFrame 中 sequence_header_tag = packaged_frame.sequence_header_tag（拷贝）。

 改为：
 sequence_header_tag = std::move(packaged_frame.sequence_header_tag);
 flv_tag = std::move(packaged_frame.flv_tag);

 6. BuildH264Outputs / BuildH265Outputs 中 access_unit 和 length_prefixed_sample 避免双重构建

 文件：stream_hub_stream_state.cpp:62–108

 BuildH264AvccSample 和 BuildH264AnnexBAccessUnit 各自独立扫描 NAL list 并分配 string，
 而两者来自同一 ParsedFramePayload，可合并为一次遍历双输出函数（stream_codec 层改动）。

 ---
 P2：ParsedFramePayload 减少堆分配

 7. ParsedFramePayload 使用 small_vector 或固定栈数组

 文件：stream_hub_stream_state.h:44–48

 大多数帧只含 1–4 个 NAL unit（P 帧只有一个 slice NAL，I 帧含 SPS+PPS+slice）。
 可改用 absl::InlinedVector 或自实现简单 SmallVector<T, 8>，避免绝大多数帧触发堆分配：

 struct ParsedFramePayload {
   VideoCodec codec = VideoCodec::kH264;
   SmallVector<stream_codec::H264NalUnit, 8> h264_units;
   SmallVector<stream_codec::H265NalUnit, 8> h265_units;
 };

 若不引入第三方库，可用：
 std::vector<H264NalUnit> h264_units;
 // 在 ParseFramePayload 调用前 reserve(8)，并复用同一对象（per-thread）

 ---
 执行顺序

 1. P0.1 AppendBytes 一行改动 → 立即生效，零风险
 2. P0.3 BuildPesPacket 改 in-place append → 减少最大的临时 string
 3. P0.4 StartSegment body.reserve → 减少 realloc
 4. P0.2 RTP 分片消除 fragment vector → 涉及签名但影响范围仅限 stream_mux
 5. P1.5 ProcessFrame 中 std::move 修复
 6. P1.6 合并双路 NAL 扫描（stream_codec 层）
 7. P2.7 SmallVector 重构（工作量最大，收益视帧率而定）
