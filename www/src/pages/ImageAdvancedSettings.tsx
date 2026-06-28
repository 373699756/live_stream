import type { ImageCapabilities, ImageConfig } from '../api/types';
import { FormField } from '../components/FormField';
import {
    numericCapability,
    optionCapability,
    OptionField,
    RangeField,
} from './ImageConfigFields';
import {
    ImageColorSettings,
    type ImageColorSection,
} from './ImageColorSettings';
import {
    defaultLensCorrection,
    defaultStabilization,
    ImageCorrectionSettings,
} from './ImageCorrectionSettings';

type ImageRecordSection =
    | ImageColorSection
    | 'backlight';

type LensCorrectionConfig = NonNullable<ImageConfig['lens_correction']>;
type StabilizationConfig = NonNullable<ImageConfig['stabilization']>;

function numberValue(
    record: Record<string, unknown>,
    key: string,
    fallback: number,
): number {
    const value = record[key];
    return typeof value === 'number' ? value : fallback;
}

function stringValue(
    record: Record<string, unknown>,
    key: string,
    fallback: string,
): string {
    const value = record[key];
    return typeof value === 'string' ? value : fallback;
}

interface ImageAdvancedSettingsProps {
    capabilities: ImageCapabilities;
    config: ImageConfig;
    onColorModeChange: (mode: string) => void;
    onOrientationChange: (orientation: ImageConfig['orientation']) => void;
    onSectionChange: (
        section: ImageRecordSection,
        key: string,
        value: unknown,
    ) => void;
    onLensCorrectionChange: (lensCorrection: LensCorrectionConfig) => void;
    onStabilizationChange: (stabilization: StabilizationConfig) => void;
}

export function ImageAdvancedSettings({
    capabilities,
    config,
    onColorModeChange,
    onOrientationChange,
    onSectionChange,
    onLensCorrectionChange,
    onStabilizationChange,
}: ImageAdvancedSettingsProps) {
    const showOrientationControls =
        capabilities.orientation.mirror || capabilities.orientation.flip;
    const lensCorrection = config.lens_correction || defaultLensCorrection;
    const stabilization = config.stabilization || defaultStabilization;

    return (
        <div className="image-advanced-list">
            <ImageColorSettings
                capabilities={capabilities}
                config={config}
                onSectionChange={onSectionChange}
            />

            <ImageCorrectionSettings
                capabilities={capabilities}
                lensCorrection={lensCorrection}
                stabilization={stabilization}
                onLensCorrectionChange={onLensCorrectionChange}
                onStabilizationChange={onStabilizationChange}
            />

            <details className="image-advanced-section">
                <summary>背光与日夜</summary>
                <div className="form-grid image-settings-grid image-detail-grid">
                    <OptionField
                        label="背光模式"
                        capability={optionCapability(
                            capabilities.backlight.options,
                            'mode',
                            ['off', 'drc'],
                        )}
                        value={stringValue(config.backlight, 'mode', 'off')}
                        onChange={(value) =>
                            onSectionChange('backlight', 'mode', value)
                        }
                    />
                    <RangeField
                        label="背光等级"
                        capability={
                            numericCapability(
                                capabilities.backlight.ranges,
                                'level',
                            ) || {
                                min: 0,
                                max: 100,
                                default: 50,
                            }
                        }
                        value={numberValue(config.backlight, 'level', 50)}
                        onChange={(value) =>
                            onSectionChange('backlight', 'level', value)
                        }
                    />
                    {capabilities.color_mode.mode &&
                        capabilities.color_mode.mode.live_update_supported !==
                            false && (
                            <OptionField
                                label="日夜模式"
                                capability={optionCapability(
                                    capabilities.color_mode,
                                    'mode',
                                    ['color', 'black_white', 'auto'],
                                )}
                                value={
                                    config.color_mode.mode ||
                                    capabilities.color_mode.mode.default
                                }
                                onChange={onColorModeChange}
                            />
                        )}
                </div>
            </details>

            {showOrientationControls && (
                <details className="image-advanced-section">
                    <summary>方向</summary>
                    <div className="form-grid image-settings-grid image-detail-grid">
                        {capabilities.orientation.mirror && (
                            <FormField label="镜像">
                                <input
                                    type="checkbox"
                                    checked={config.orientation.mirror}
                                    onChange={(e) =>
                                        onOrientationChange({
                                            ...config.orientation,
                                            mirror: e.target.checked,
                                        })
                                    }
                                />
                            </FormField>
                        )}
                        {capabilities.orientation.flip && (
                            <FormField label="翻转">
                                <input
                                    type="checkbox"
                                    checked={config.orientation.flip}
                                    onChange={(e) =>
                                        onOrientationChange({
                                            ...config.orientation,
                                            flip: e.target.checked,
                                        })
                                    }
                                />
                            </FormField>
                        )}
                    </div>
                </details>
            )}
        </div>
    );
}
