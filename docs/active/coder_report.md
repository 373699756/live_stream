# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

把 HiSilicon VI/MIPI/ISP 启动从硬编码 IMX290 收敛为 sensor profile，并补齐
SDK 样例中可确认的 Sony IMX 型号。

## Problem fixed

之前 `hisi_mpp_vi.cpp` 把 IMX290 的 MIPI lane、RAW bit width、ISP pub attr、
VI DEV/PIPE 尺寸写死。切到 IMX307/327/335/458 时，即使链接了对应 sensor lib，
VI/MIPI/ISP 仍不会按对应 sensor 初始化。

## Files changed

主要变更在 `libs/hisi_vendor/`：

- 新增 `src/hisi_mpp_sensor.h`，集中维护编译期 `SENSOR0_TYPE` 到 sensor profile
  的映射。
- VI MIPI attr、VI DEV attr、VI PIPE attr、ISP pub attr、sensor callback
  全部从 profile 取参数。
- SYS VB pool 和 VPSS group 最大尺寸改为按 sensor 输入尺寸配置，避免 IMX335/458
  这类大分辨率 sensor 仍按主码流 1080p 分配。
- 已按 SDK sample 参数支持 IMX290、IMX307、IMX327、IMX327 2-lane、IMX335、
  IMX458 的线性/已列出的 WDR 编译期 profile。
- 删除长参数 profile helper，改为具名 profile 构造函数加少量明确的字段修改函数。

## Behavior changed

- 默认仍是 `SONY_IMX290_MIPI_2M_30FPS_12BIT`。
- 通过 `make -C libs/hisi_vendor SENSOR0_TYPE=<sensor_macro>` 可切换编译期
  sensor profile。
- 大尺寸 sensor 启动时 MIPI/VI/ISP/VPSS/VB 使用真实 sensor 输入尺寸；主码流、
  子码流输出尺寸仍由业务配置控制。

## Verification

通过：

- `make -B -C libs/hisi_vendor`
- `make -B -C libs/hisi_vendor SENSOR0_TYPE=SONY_IMX335_MIPI_5M_30FPS_12BIT`
- `make -B -C libs/hisi_vendor SENSOR0_TYPE=SONY_IMX458_MIPI_8M_30FPS_10BIT`
- `make -B -C libs/hisi_vendor SENSOR0_TYPE=SONY_IMX290_MIPI_2M_30FPS_10BIT_WDR2TO1`
- `make -B -C libs/hisi_vendor SENSOR0_TYPE=SONY_IMX327_2L_MIPI_2M_30FPS_12BIT`
- 最后再次 `make -B -C libs/hisi_vendor` 恢复默认 IMX290 产物
- `git diff --check -- libs/hisi_vendor/src/hisi_mpp_sensor.h libs/hisi_vendor/src/hisi_mpp_vi.cpp libs/hisi_vendor/src/hisi_mpp_sys.cpp libs/hisi_vendor/src/hisi_mpp_vpss.cpp`

未做：

- 板端实测。MIPI lane、sensor reset/clock、镜头模组实际接线和 sensor I2C
  地址仍需要在目标板验证。

## Commit

Pending.

## Deviations

sensor 选择仍保持编译期 `SENSOR0_TYPE`，没有做成运行时 UI 配置。原因是 sensor
切换涉及 MIPI/VI/ISP 启动链路和物理接线，运行时热切换风险高。

## Blocked or follow-up

新增其他 IMX 型号时，先从 SDK sample 或 sensor lib 文档确认 MIPI attr、
VI DEV/PIPE attr、ISP pub attr 和 sensor object，再补一个 profile 函数并用
`SENSOR0_TYPE=<macro>` 做单模块强制构建。
