#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_SENSOR_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_SENSOR_H_

#include "hisi_mpp_utils.h"

#include <cstdint>

#ifndef SONY_IMX327_MIPI_2M_30FPS_12BIT
#define SONY_IMX327_MIPI_2M_30FPS_12BIT 0
#endif
#ifndef SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1
#define SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1 1
#endif
#ifndef SONY_IMX327_2L_MIPI_2M_30FPS_12BIT
#define SONY_IMX327_2L_MIPI_2M_30FPS_12BIT 2
#endif
#ifndef SONY_IMX327_2L_MIPI_2M_30FPS_12BIT_WDR2TO1
#define SONY_IMX327_2L_MIPI_2M_30FPS_12BIT_WDR2TO1 3
#endif
#ifndef SONY_IMX307_MIPI_2M_30FPS_12BIT
#define SONY_IMX307_MIPI_2M_30FPS_12BIT 4
#endif
#ifndef SONY_IMX307_MIPI_2M_30FPS_12BIT_WDR2TO1
#define SONY_IMX307_MIPI_2M_30FPS_12BIT_WDR2TO1 5
#endif
#ifndef SONY_IMX335_MIPI_5M_30FPS_12BIT
#define SONY_IMX335_MIPI_5M_30FPS_12BIT 6
#endif
#ifndef SONY_IMX335_MIPI_5M_30FPS_10BIT_WDR2TO1
#define SONY_IMX335_MIPI_5M_30FPS_10BIT_WDR2TO1 7
#endif
#ifndef SONY_IMX335_MIPI_4M_30FPS_12BIT
#define SONY_IMX335_MIPI_4M_30FPS_12BIT 8
#endif
#ifndef SONY_IMX335_MIPI_4M_30FPS_10BIT_WDR2TO1
#define SONY_IMX335_MIPI_4M_30FPS_10BIT_WDR2TO1 9
#endif
#ifndef SONY_IMX458_MIPI_8M_30FPS_10BIT
#define SONY_IMX458_MIPI_8M_30FPS_10BIT 10
#endif
#ifndef SONY_IMX458_MIPI_12M_20FPS_10BIT
#define SONY_IMX458_MIPI_12M_20FPS_10BIT 11
#endif
#ifndef SONY_IMX458_MIPI_4M_60FPS_10BIT
#define SONY_IMX458_MIPI_4M_60FPS_10BIT 12
#endif
#ifndef SONY_IMX458_MIPI_4M_40FPS_10BIT
#define SONY_IMX458_MIPI_4M_40FPS_10BIT 13
#endif
#ifndef SONY_IMX458_MIPI_2M_90FPS_10BIT
#define SONY_IMX458_MIPI_2M_90FPS_10BIT 14
#endif
#ifndef SONY_IMX458_MIPI_1M_129FPS_10BIT
#define SONY_IMX458_MIPI_1M_129FPS_10BIT 15
#endif
#ifndef SONY_IMX290_MIPI_2M_30FPS_12BIT
#define SONY_IMX290_MIPI_2M_30FPS_12BIT 19
#endif
#ifndef SONY_IMX290_MIPI_2M_30FPS_10BIT_WDR2TO1
#define SONY_IMX290_MIPI_2M_30FPS_10BIT_WDR2TO1 20
#endif

namespace live_stream {
namespace hisisdk {
namespace internal {

struct SensorProfile {
    const char* name = "imx290";
    ISP_SNS_OBJ_S* sns_obj = &stSnsImx290Obj;
    uint32_t mipi_x = 0;
    uint32_t mipi_y = 0;
    uint32_t input_width = 1920;
    uint32_t input_height = 1080;
    uint32_t sensor_width = 1920;
    uint32_t sensor_height = 1080;
    float frame_rate = 30.0f;
    PIXEL_FORMAT_E pipe_pixel_format = PIXEL_FORMAT_RGB_BAYER_12BPP;
    COMPRESS_MODE_E pipe_compress_mode = COMPRESS_MODE_NONE;
    DATA_BITWIDTH_E pipe_bit_width = DATA_BITWIDTH_12;
    HI_BOOL pipe_nr_enabled = HI_FALSE;
    data_type_t mipi_data_type = DATA_TYPE_RAW_12BIT;
    ISP_BAYER_FORMAT_E bayer = BAYER_RGGB;
    WDR_MODE_E wdr_mode = WDR_MODE_NONE;
    mipi_wdr_mode_t mipi_wdr_mode = HI_MIPI_WDR_MODE_NONE;
    uint32_t vi_component_mask = 0xFFF00000;
    HI_U8 sns_mode = 0;
    HI_S8 i2c_device = 0;
    short lane_id[MIPI_LANE_NUM] = {0, 1, 2, 3};
};

inline SensorProfile MakeBase2mProfile(const char* name,
                                       ISP_SNS_OBJ_S* sns_obj) {
    SensorProfile profile{};
    profile.name = name;
    profile.sns_obj = sns_obj;
    return profile;
}

inline void SetInputSize(SensorProfile& profile, uint32_t width,
                         uint32_t height) {
    profile.input_width = width;
    profile.input_height = height;
    profile.sensor_width = width;
    profile.sensor_height = height;
}

inline void SetSensorSize(SensorProfile& profile, uint32_t width,
                          uint32_t height) {
    profile.sensor_width = width;
    profile.sensor_height = height;
}

inline void UseRaw10(SensorProfile& profile) {
    profile.pipe_pixel_format = PIXEL_FORMAT_RGB_BAYER_10BPP;
    profile.pipe_bit_width = DATA_BITWIDTH_10;
    profile.mipi_data_type = DATA_TYPE_RAW_10BIT;
}

inline void UseLineCompression(SensorProfile& profile) {
    profile.pipe_compress_mode = COMPRESS_MODE_LINE;
}

inline void UseVpssPipeNr(SensorProfile& profile) {
    profile.pipe_nr_enabled = HI_TRUE;
}

inline void UseWdr2To1(SensorProfile& profile,
                       mipi_wdr_mode_t mipi_wdr_mode) {
    profile.wdr_mode = WDR_MODE_2To1_LINE;
    profile.mipi_wdr_mode = mipi_wdr_mode;
    profile.vi_component_mask = 0xFFC00000;
}

inline void UseMipi2LaneCh0(SensorProfile& profile) {
    profile.lane_id[0] = 0;
    profile.lane_id[1] = 2;
    profile.lane_id[2] = -1;
    profile.lane_id[3] = -1;
}

inline SensorProfile MakeImx327Profile() {
    return MakeBase2mProfile("imx327", &stSnsImx327Obj);
}

inline SensorProfile MakeImx327WdrProfile() {
    SensorProfile profile = MakeImx327Profile();
    profile.name = "imx327_wdr2to1";
    profile.pipe_pixel_format = PIXEL_FORMAT_RGB_BAYER_10BPP;
    profile.pipe_bit_width = DATA_BITWIDTH_10;
    profile.mipi_data_type = DATA_TYPE_RAW_12BIT;
    UseWdr2To1(profile, HI_MIPI_WDR_MODE_DOL);
    return profile;
}

inline SensorProfile MakeImx3272LaneProfile() {
    SensorProfile profile = MakeBase2mProfile("imx327_2lane",
                                              &stSnsImx327_2l_Obj);
    UseMipi2LaneCh0(profile);
    return profile;
}

inline SensorProfile MakeImx3272LaneWdrProfile() {
    SensorProfile profile = MakeImx3272LaneProfile();
    profile.name = "imx327_2lane_wdr2to1";
    profile.pipe_pixel_format = PIXEL_FORMAT_RGB_BAYER_10BPP;
    profile.pipe_bit_width = DATA_BITWIDTH_10;
    profile.mipi_data_type = DATA_TYPE_RAW_12BIT;
    UseWdr2To1(profile, HI_MIPI_WDR_MODE_DOL);
    return profile;
}

inline SensorProfile MakeImx307Profile() {
    return MakeBase2mProfile("imx307", &stSnsImx307Obj);
}

inline SensorProfile MakeImx307WdrProfile() {
    SensorProfile profile = MakeImx307Profile();
    profile.name = "imx307_wdr2to1";
    profile.pipe_pixel_format = PIXEL_FORMAT_RGB_BAYER_10BPP;
    profile.pipe_bit_width = DATA_BITWIDTH_10;
    profile.mipi_data_type = DATA_TYPE_RAW_12BIT;
    UseWdr2To1(profile, HI_MIPI_WDR_MODE_DOL);
    return profile;
}

inline SensorProfile MakeImx3355mProfile() {
    SensorProfile profile = MakeBase2mProfile("imx335_5m", &stSnsImx335Obj);
    SetInputSize(profile, 2592, 1944);
    UseLineCompression(profile);
    return profile;
}

inline SensorProfile MakeImx3355mWdrProfile() {
    SensorProfile profile = MakeImx3355mProfile();
    profile.name = "imx335_5m_wdr2to1";
    UseRaw10(profile);
    UseWdr2To1(profile, HI_MIPI_WDR_MODE_VC);
    return profile;
}

inline SensorProfile MakeImx3354mProfile() {
    SensorProfile profile = MakeBase2mProfile("imx335_4m", &stSnsImx335Obj);
    SetInputSize(profile, 2592, 1536);
    SetSensorSize(profile, 2592, 1944);
    profile.mipi_y = 204;
    UseLineCompression(profile);
    return profile;
}

inline SensorProfile MakeImx3354mWdrProfile() {
    SensorProfile profile = MakeImx3354mProfile();
    profile.name = "imx335_4m_wdr2to1";
    UseRaw10(profile);
    UseWdr2To1(profile, HI_MIPI_WDR_MODE_VC);
    return profile;
}

inline SensorProfile MakeBaseImx458Profile(const char* name) {
    SensorProfile profile = MakeBase2mProfile(name, &stSnsImx458Obj);
    profile.vi_component_mask = 0xFFC00000;
    UseRaw10(profile);
    UseLineCompression(profile);
    UseVpssPipeNr(profile);
    return profile;
}

inline SensorProfile MakeImx4588mProfile() {
    SensorProfile profile = MakeBaseImx458Profile("imx458_8m");
    SetInputSize(profile, 3840, 2160);
    return profile;
}

inline SensorProfile MakeImx45812mProfile() {
    SensorProfile profile = MakeBaseImx458Profile("imx458_12m");
    SetInputSize(profile, 4000, 3000);
    profile.frame_rate = 20.0f;
    return profile;
}

inline SensorProfile MakeImx4584m60FpsProfile() {
    SensorProfile profile = MakeBaseImx458Profile("imx458_4m_60fps");
    SetInputSize(profile, 2716, 1524);
    profile.frame_rate = 60.0f;
    return profile;
}

inline SensorProfile MakeImx4584m40FpsProfile() {
    SensorProfile profile = MakeBaseImx458Profile("imx458_4m_40fps");
    SetInputSize(profile, 2716, 1524);
    profile.frame_rate = 40.0f;
    profile.sns_mode = 1;
    return profile;
}

inline SensorProfile MakeImx4582m90FpsProfile() {
    SensorProfile profile = MakeBaseImx458Profile("imx458_2m_90fps");
    profile.frame_rate = 90.0f;
    return profile;
}

inline SensorProfile MakeImx4581m129FpsProfile() {
    SensorProfile profile = MakeBaseImx458Profile("imx458_1m_129fps");
    SetInputSize(profile, 1280, 720);
    profile.frame_rate = 129.0f;
    return profile;
}

inline SensorProfile MakeImx290WdrProfile() {
    SensorProfile profile = MakeBase2mProfile("imx290_wdr2to1",
                                              &stSnsImx290Obj);
    UseRaw10(profile);
    UseLineCompression(profile);
    UseVpssPipeNr(profile);
    UseWdr2To1(profile, HI_MIPI_WDR_MODE_DOL);
    return profile;
}

inline SensorProfile MakeImx290Profile() {
    return MakeBase2mProfile("imx290", &stSnsImx290Obj);
}

inline SensorProfile MakeSelectedSensorProfile() {
#ifndef SENSOR0_TYPE
#error "SENSOR0_TYPE must be defined by the HiSilicon toolchain configuration"
#endif
#if SENSOR0_TYPE == SONY_IMX327_MIPI_2M_30FPS_12BIT
    return MakeImx327Profile();
#elif SENSOR0_TYPE == SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1
    return MakeImx327WdrProfile();
#elif SENSOR0_TYPE == SONY_IMX327_2L_MIPI_2M_30FPS_12BIT
    return MakeImx3272LaneProfile();
#elif SENSOR0_TYPE == SONY_IMX327_2L_MIPI_2M_30FPS_12BIT_WDR2TO1
    return MakeImx3272LaneWdrProfile();
#elif SENSOR0_TYPE == SONY_IMX307_MIPI_2M_30FPS_12BIT
    return MakeImx307Profile();
#elif SENSOR0_TYPE == SONY_IMX307_MIPI_2M_30FPS_12BIT_WDR2TO1
    return MakeImx307WdrProfile();
#elif SENSOR0_TYPE == SONY_IMX335_MIPI_5M_30FPS_12BIT
    return MakeImx3355mProfile();
#elif SENSOR0_TYPE == SONY_IMX335_MIPI_5M_30FPS_10BIT_WDR2TO1
    return MakeImx3355mWdrProfile();
#elif SENSOR0_TYPE == SONY_IMX335_MIPI_4M_30FPS_12BIT
    return MakeImx3354mProfile();
#elif SENSOR0_TYPE == SONY_IMX335_MIPI_4M_30FPS_10BIT_WDR2TO1
    return MakeImx3354mWdrProfile();
#elif SENSOR0_TYPE == SONY_IMX458_MIPI_8M_30FPS_10BIT
    return MakeImx4588mProfile();
#elif SENSOR0_TYPE == SONY_IMX458_MIPI_12M_20FPS_10BIT
    return MakeImx45812mProfile();
#elif SENSOR0_TYPE == SONY_IMX458_MIPI_4M_60FPS_10BIT
    return MakeImx4584m60FpsProfile();
#elif SENSOR0_TYPE == SONY_IMX458_MIPI_4M_40FPS_10BIT
    return MakeImx4584m40FpsProfile();
#elif SENSOR0_TYPE == SONY_IMX458_MIPI_2M_90FPS_10BIT
    return MakeImx4582m90FpsProfile();
#elif SENSOR0_TYPE == SONY_IMX458_MIPI_1M_129FPS_10BIT
    return MakeImx4581m129FpsProfile();
#elif SENSOR0_TYPE == SONY_IMX290_MIPI_2M_30FPS_10BIT_WDR2TO1
    return MakeImx290WdrProfile();
#else
#error "Unsupported SENSOR0_TYPE; add an explicit SensorProfile mapping"
#endif
}

inline const SensorProfile& SelectedSensorProfile() {
    static const SensorProfile profile = MakeSelectedSensorProfile();
    return profile;
}

}  // namespace internal
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_SENSOR_H_
