# HiSilicon SVP Local Dependencies

本目录保存 Hi3516DV300/Hi3516CV500 SDK 中和 SVP/NNIE/IVE AI 开发直接相关的
离线参考资源。生产代码默认不直接编译这里的 sample；当前项目编译使用
`3rdparty/hisi_mpp/include` 和 `3rdparty/hisi_mpp/lib` 中已经本地化的 MPP、NNIE、
IVE 头文件和库。

## Source

- SDK 文档来源：
  `/home/cp/Public/hisi/sdk_hisi3516dv300/zh/01.software/board/SVP`
- SDK 示例来源：
  `/home/cp/Public/hisi/sdk_hisi3516dv300/Hi3516CV500_SDK_V2.0.1.0/smp/a7_linux/mpp/sample/svp`
- 拷贝日期：2026-05-24

## Contents

- `docs/`：HiSVP、HiIVE、HiIVS API 参考和 HiSVP 开发指南 PDF。
- `sample/svp/common/`：SVP sample 公共初始化、内存、文件和 NNIE/IVE 辅助代码。
- `sample/svp/nnie/`：NNIE 模型加载、Forward、后处理示例、`.wk` 模型和输入样例。
- `sample/svp/ive/`：IVE 移动侦测、边缘、KCF、GMM2、透视变换等 sample 和输入样例。
- `sample/svp/hirt/`：HiRuntime SSD/RFCN/classify 示例、插件和 runtime `.wk` 资源。

## Model Resources

NNIE sample 模型位于：

- `sample/svp/nnie/data/nnie_model/classification/inst_mnist_cycle.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_ssd_cycle.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_yolov1_cycle.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_yolov2_cycle.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_yolov3_cycle.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_rfcn_cycle.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_rfcn_resnet50_cycle.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_rfcn_resnet50_cycle_352x288.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_alexnet_frcnn_cycle.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_fasterrcnn_pvanet_inst.wk`
- `sample/svp/nnie/data/nnie_model/detection/inst_fasterrcnn_double_roipooling_cycle.wk`
- `sample/svp/nnie/data/nnie_model/segmentation/inst_segnet_cycle.wk`
- `sample/svp/nnie/data/nnie_model/recurrent/lstm_3_3.wk`

HiRuntime sample 模型位于：

- `sample/svp/hirt/resource/runtime_ssd_inst.wk`
- `sample/svp/hirt/resource/runtime_rfcn_resnet50_inst.wk`
- `sample/svp/hirt/resource/runtime_alexnet_no_group_inst.wk`

IVE KCF sample 使用的 NNIE 模型位于：

- `sample/svp/ive/data/input/kcf/inst_rfcn_resnet50_cycle_352x288.wk`

## Development Notes

- `ai_service` 当前直接使用 NNIE MPI 接口，不依赖 sample Makefile。
- 后处理实现应优先参考：
  `sample/svp/nnie/sample/sample_nnie.c` 和
  `sample/svp/nnie/sample_nnie_software/sample_svp_nnie_software.c`。
- 输入 resize、YUV/RGB 转换和运动检测应优先参考：
  `sample/svp/ive/sample/` 和 `sample/svp/common/sample_comm_ive.c`。
- HiRuntime 方向只作为后续评估参考；当前默认后端仍是
  `hisi3516dv300_nnie`。
