# 重构 AI：VDS 接入验证计划

## 结论

这条路线不能理解成“把库和模型整体拷进项目就能用”。当前 `live_stream`
的 AI 后端只直接支持已有 NNIE/IVE 路线，外部 VDS 模型必须先通过 Dahua
VDS SDK 的调用链验证。

这次扫描的核心价值是：不用从零猜 VDS 怎么调用。
`alg_test` 已经给了完整调用样板，下一步应该从它抽一个最小 `vds_probe`，
先验证 SDK、配置和模型在 Hi3516DV300 板端能不能跑通。

## 当前 AI 限制

`live_stream` 现有 AI 配置里的 `model_path` 不能直接吃这批 VDS 模型。

原因是现有后端只对一种硬编码 SSD 输出结构做了解码：

- 输入固定按当前 SSD 模型处理。
- 输出只识别当前 SSD 的 blob 布局。
- `motion_classification` 和 `occlusion_detection` 走 MD/IVE，不依赖 VDS `.wk`。
- 如果外部模型能加载但输出结构不匹配，最终也不会产生有效 `AiDetection`。

所以不要把 `Images/algCfg/model/*.wk` 直接塞给现有 `ai.model_path`。

## 可用资料

优先使用这套资料做验证：

- 调用样板：
  `/home/cp/Public/hisi/hisi3516/LibSys/Intelligent_LibSys/P_2018.05.02_SysIntelliApi_V2/test/alg_test`
- Hi3516DV300 库：
  `/home/cp/Public/hisi/hisi3516/LibSys/Intelligent_LibSys/P_2018.05.02_SysIntelliApi_V2/test/alg_test/libs/hi3516dv300`
- VDS 配置：
  `/home/cp/Public/hisi/hisi3516/P_2018.10.30_Project_IPCHisilicon_Hi3516DV300/Images/algCfg/bmdvdsCfg/ivs_model_cfg`
- VDS 模型目录：
  `/home/cp/Public/hisi/hisi3516/P_2018.10.30_Project_IPCHisilicon_Hi3516DV300/Images/algCfg/model`

关键参考文件：

- `test/alg_test/comon/alg_vds_dl.c`
- `test/alg_test/inc/alg_vds_dl.h`
- `test/alg_test/test_case/bmd_testcase.c`
- `include/alg_include/common/dhivs_define.h`
- `include/alg_include/common/dhivs_base_define.h`

## 阶段 1：最小 vds_probe

先从 `alg_test` 抽一个最小探针，不接入 `live_stream` 主流程。

探针只做这几个调用：

```text
DHIVS_VDS_SetCfgFile
DHIVS_VDS_Init
DHIVS_VDS_GetCap
DHIVS_VDS_CreateHandle
DHIVS_VDS_UnInit
```

建议优先用 `dlopen/dlsym`，参考 `alg_vds_dl.c`，避免一开始把主程序硬链接到
VDS SDK。

本阶段成功标准：

- 板端能加载 `libdhivs_sdk_vds_debug.so`。
- `SetCfgFile/Init/GetCap/CreateHandle` 全部返回成功。
- 能打印 VDS capability 关键字段。
- `UnInit` 能正常释放。

本阶段失败就先停，不进入 `live_stream` 集成。

## 阶段 2：接 ProcessImageMulti/GetResults

初始化链路通过后，再接单帧 YUV 输入。

参考 `bmd_testcase.c`：

- 按 `Bmd_FrameToImg()` 把 YUV frame 转成 `dhivs_pic_v2_t`。
- 设置 `color_space = DHIVS_COLORSPACE_YUV420SP_VU`。
- 填好 width、height、stride、uv_stride、虚拟地址和物理地址。
- 调用 `ProcessImageMulti`。
- 调用 `GetResults`。
- 遍历 `dhivs_detect_result_t::obj_ptr`。
- 最后调用 `ReleaseResults`。

本阶段成功标准：

- `ProcessImageMulti` 返回成功。
- `GetResults` 能拿到 `imageNum`。
- 能打印每个目标的 `type`、`conf`、`rect.ul/lr`。
- `ReleaseResults` 正常释放结果。

## 阶段 3：接入 live_stream AiDetection

只有前两个阶段都过了，才开始改 `libs/ai`。

接入方式：

- 新增可选 VDS 后端，不替换现有 NNIE SSD 后端。
- VDS 后端内部持有 VDS handle 和 SDK 动态加载符号。
- 把项目里的 YUV 帧转换成 `dhivs_pic_v2_t`。
- 调用 `ProcessImageMulti/GetResults/ReleaseResults`。
- 把 `obj_ptr[i].type/rect/conf` 映射成 `AiDetection`。

坐标处理：

- 如果 VDS 返回 1024 相对坐标，要按当前帧宽高转成像素坐标。
- 如果返回的是像素坐标，直接按帧尺寸裁剪到有效范围。

对象类型处理：

- 先只映射项目能消费的基础目标，例如人、车、非机动车、人脸。
- 其他 VDS 类型先保留为通用 label 或忽略，避免误触发业务告警。

## 成功后的集成边界

VDS 后端应该是可选能力：

- 不破坏当前 `object_detection` SSD 路线。
- 不影响 `motion_classification` 和 `occlusion_detection`。
- VDS 初始化失败只让 AI 后端不可用，不影响直播主链路。
- 高频帧路径不加普通日志，只保留错误和关键状态变化。

## 失败回退

如果 `vds_probe` 初始化失败，说明库、配置、模型或板端 ABI 不匹配，停止 VDS 路线。

如果初始化成功但 `ProcessImageMulti/GetResults` 失败，优先检查：

- 配置文件引用的模型路径是否能在板端找到。
- `LD_LIBRARY_PATH` 是否包含 VDS 依赖库目录。
- YUV 格式是否和 VDS 要求一致。
- 物理地址和 stride 是否正确。
- `caps` 配置是否和 `alg_test` 保持一致。

如果仍然失败，回到原始 NNIE 路线，不继续把黑盒 VDS 强行接入主程序。

## 下一步

实际动手顺序：

1. 新建最小 `vds_probe`。
2. 使用 `test/alg_test/libs/hi3516dv300` 这套库。
3. 使用 `P_2018.10.30.../Images/algCfg/bmdvdsCfg/ivs_model_cfg` 和对应模型目录。
4. 先只跑 `SetCfgFile/Init/GetCap/CreateHandle/UnInit`。
5. 通过后再接 `ProcessImageMulti/GetResults`。
6. 最后把 `obj_ptr[i].type/rect/conf` 映射进 `live_stream` 的 `AiDetection`。
