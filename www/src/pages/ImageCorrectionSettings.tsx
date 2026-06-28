import type { ImageCapabilities, ImageConfig } from '../api/types';
import { FormField } from '../components/FormField';
import {
    numericCapability,
    optionCapability,
    OptionField,
    RangeField,
} from './ImageConfigFields';

type LensCorrectionConfig = NonNullable<ImageConfig['lens_correction']>;
type StabilizationConfig = NonNullable<ImageConfig['stabilization']>;
type LensCorrectionCapabilities = NonNullable<
    ImageCapabilities['lens_correction']
>;
type StabilizationCapabilities = NonNullable<
    ImageCapabilities['stabilization']
>;

interface ImageCorrectionSettingsProps {
    capabilities: ImageCapabilities;
    lensCorrection: LensCorrectionConfig;
    stabilization: StabilizationConfig;
    onLensCorrectionChange: (lensCorrection: LensCorrectionConfig) => void;
    onStabilizationChange: (stabilization: StabilizationConfig) => void;
}

export const defaultLensCorrection: LensCorrectionConfig = {
    enabled: false,
    aspect: true,
    x_ratio: 100,
    y_ratio: 100,
    xy_ratio: 100,
    center_x_offset: 0,
    center_y_offset: 0,
    distortion_ratio: 0,
};

export const defaultStabilization: StabilizationConfig = {
    enabled: false,
    motion_level: 'normal',
    crop_ratio: 80,
    buffer_frames: 6,
    frame_rate: 30,
    moving_subject_level: 0,
    rolling_shutter_coef: 0,
    horizontal_limit: 512,
    vertical_limit: 512,
};

const defaultLensCorrectionCapabilities: LensCorrectionCapabilities = {
    supported: false,
    min_width: 0,
    min_height: 0,
    options: {},
    ranges: {},
};

const defaultStabilizationCapabilities: StabilizationCapabilities = {
    supported: false,
    min_width: 0,
    min_height: 0,
    options: {},
    ranges: {},
};

export function ImageCorrectionSettings({
    capabilities,
    lensCorrection,
    stabilization,
    onLensCorrectionChange,
    onStabilizationChange,
}: ImageCorrectionSettingsProps) {
    const lensCorrectionCapabilities =
        capabilities.lens_correction || defaultLensCorrectionCapabilities;
    const stabilizationCapabilities =
        capabilities.stabilization || defaultStabilizationCapabilities;

    const updateLensCorrection = (value: Partial<LensCorrectionConfig>) => {
        onLensCorrectionChange({ ...lensCorrection, ...value });
    };
    const updateStabilization = (value: Partial<StabilizationConfig>) => {
        onStabilizationChange({ ...stabilization, ...value });
    };

    return (
        <>
            {lensCorrectionCapabilities.supported && (
                <details className="image-advanced-section">
                    <summary>镜头畸变校正</summary>
                    <div className="form-grid image-settings-grid image-detail-grid">
                        <FormField label="启用">
                            <input
                                type="checkbox"
                                checked={lensCorrection.enabled}
                                onChange={(e) =>
                                    updateLensCorrection({
                                        enabled: e.target.checked,
                                    })
                                }
                            />
                        </FormField>
                        {lensCorrectionCapabilities.options.aspect && (
                            <FormField label="保持比例">
                                <input
                                    type="checkbox"
                                    checked={lensCorrection.aspect}
                                    onChange={(e) =>
                                        updateLensCorrection({
                                            aspect: e.target.checked,
                                        })
                                    }
                                />
                            </FormField>
                        )}
                        <RangeField
                            label="横向视角"
                            capability={
                                numericCapability(
                                    lensCorrectionCapabilities.ranges,
                                    'x_ratio',
                                ) || { min: 0, max: 100, default: 100 }
                            }
                            value={lensCorrection.x_ratio}
                            onChange={(value) =>
                                updateLensCorrection({ x_ratio: value })
                            }
                        />
                        <RangeField
                            label="纵向视角"
                            capability={
                                numericCapability(
                                    lensCorrectionCapabilities.ranges,
                                    'y_ratio',
                                ) || { min: 0, max: 100, default: 100 }
                            }
                            value={lensCorrection.y_ratio}
                            onChange={(value) =>
                                updateLensCorrection({ y_ratio: value })
                            }
                        />
                        <RangeField
                            label="整体视角"
                            capability={
                                numericCapability(
                                    lensCorrectionCapabilities.ranges,
                                    'xy_ratio',
                                ) || { min: 0, max: 100, default: 100 }
                            }
                            value={lensCorrection.xy_ratio}
                            onChange={(value) =>
                                updateLensCorrection({ xy_ratio: value })
                            }
                        />
                        <RangeField
                            label="中心X偏移"
                            capability={
                                numericCapability(
                                    lensCorrectionCapabilities.ranges,
                                    'center_x_offset',
                                ) || { min: -511, max: 511, default: 0 }
                            }
                            value={lensCorrection.center_x_offset}
                            onChange={(value) =>
                                updateLensCorrection({ center_x_offset: value })
                            }
                        />
                        <RangeField
                            label="中心Y偏移"
                            capability={
                                numericCapability(
                                    lensCorrectionCapabilities.ranges,
                                    'center_y_offset',
                                ) || { min: -511, max: 511, default: 0 }
                            }
                            value={lensCorrection.center_y_offset}
                            onChange={(value) =>
                                updateLensCorrection({ center_y_offset: value })
                            }
                        />
                        <RangeField
                            label="畸变强度"
                            capability={
                                numericCapability(
                                    lensCorrectionCapabilities.ranges,
                                    'distortion_ratio',
                                ) || { min: -300, max: 500, default: 0 }
                            }
                            value={lensCorrection.distortion_ratio}
                            onChange={(value) =>
                                updateLensCorrection({
                                    distortion_ratio: value,
                                })
                            }
                        />
                    </div>
                </details>
            )}

            {stabilizationCapabilities.supported && (
                <details className="image-advanced-section">
                    <summary>电子防抖</summary>
                    <div className="form-grid image-settings-grid image-detail-grid">
                        <FormField label="启用">
                            <input
                                type="checkbox"
                                checked={stabilization.enabled}
                                onChange={(e) =>
                                    updateStabilization({
                                        enabled: e.target.checked,
                                    })
                                }
                            />
                        </FormField>
                        <OptionField
                            label="运动等级"
                            capability={optionCapability(
                                stabilizationCapabilities.options,
                                'motion_level',
                                ['low', 'normal', 'high'],
                            )}
                            value={stabilization.motion_level}
                            onChange={(value) =>
                                updateStabilization({ motion_level: value })
                            }
                        />
                        <RangeField
                            label="裁剪比例"
                            capability={
                                numericCapability(
                                    stabilizationCapabilities.ranges,
                                    'crop_ratio',
                                ) || { min: 50, max: 98, default: 80 }
                            }
                            value={stabilization.crop_ratio}
                            onChange={(value) =>
                                updateStabilization({ crop_ratio: value })
                            }
                        />
                        <RangeField
                            label="缓冲帧数"
                            capability={
                                numericCapability(
                                    stabilizationCapabilities.ranges,
                                    'buffer_frames',
                                ) || { min: 5, max: 10, default: 6 }
                            }
                            value={stabilization.buffer_frames}
                            onChange={(value) =>
                                updateStabilization({ buffer_frames: value })
                            }
                        />
                        <RangeField
                            label="处理帧率"
                            capability={
                                numericCapability(
                                    stabilizationCapabilities.ranges,
                                    'frame_rate',
                                ) || { min: 1, max: 60, default: 30 }
                            }
                            value={stabilization.frame_rate}
                            onChange={(value) =>
                                updateStabilization({ frame_rate: value })
                            }
                        />
                        <RangeField
                            label="运动主体"
                            capability={
                                numericCapability(
                                    stabilizationCapabilities.ranges,
                                    'moving_subject_level',
                                ) || { min: 0, max: 6, default: 0 }
                            }
                            value={stabilization.moving_subject_level}
                            onChange={(value) =>
                                updateStabilization({
                                    moving_subject_level: value,
                                })
                            }
                        />
                        <RangeField
                            label="卷帘校正"
                            capability={
                                numericCapability(
                                    stabilizationCapabilities.ranges,
                                    'rolling_shutter_coef',
                                ) || { min: 0, max: 1000, default: 0 }
                            }
                            value={stabilization.rolling_shutter_coef}
                            onChange={(value) =>
                                updateStabilization({
                                    rolling_shutter_coef: value,
                                })
                            }
                        />
                        <RangeField
                            label="水平漂移"
                            capability={
                                numericCapability(
                                    stabilizationCapabilities.ranges,
                                    'horizontal_limit',
                                ) || { min: 0, max: 1000, default: 512 }
                            }
                            value={stabilization.horizontal_limit}
                            onChange={(value) =>
                                updateStabilization({ horizontal_limit: value })
                            }
                        />
                        <RangeField
                            label="垂直漂移"
                            capability={
                                numericCapability(
                                    stabilizationCapabilities.ranges,
                                    'vertical_limit',
                                ) || { min: 0, max: 1000, default: 512 }
                            }
                            value={stabilization.vertical_limit}
                            onChange={(value) =>
                                updateStabilization({ vertical_limit: value })
                            }
                        />
                    </div>
                </details>
            )}
        </>
    );
}
