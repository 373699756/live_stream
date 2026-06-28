import type { ImageCapabilities, ImageConfig } from '../api/types';
import { FormField } from '../components/FormField';
import {
    numericCapability,
    optionCapability,
    OptionField,
    RangeField,
} from './ImageConfigFields';

export type ImageColorSection = 'exposure' | 'white_balance' | 'enhancement';

interface ImageColorSettingsProps {
    capabilities: ImageCapabilities;
    config: ImageConfig;
    onSectionChange: (
        section: ImageColorSection,
        key: string,
        value: unknown,
    ) => void;
}

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

function boolValue(
    record: Record<string, unknown>,
    key: string,
    fallback: boolean,
): boolean {
    const value = record[key];
    return typeof value === 'boolean' ? value : fallback;
}

function supportsOptionValue(
    controls: ImageCapabilities['exposure']['options'],
    key: string,
    value: string,
): boolean {
    const capability = controls[key];
    return capability ? capability.values.includes(value) : true;
}

export function ImageColorSettings({
    capabilities,
    config,
    onSectionChange,
}: ImageColorSettingsProps) {
    return (
        <>
            <details className="image-advanced-section">
                <summary>曝光控制</summary>
                <div className="form-grid image-settings-grid image-detail-grid">
                    <OptionField
                        label="曝光模式"
                        capability={optionCapability(
                            capabilities.exposure.options,
                            'mode',
                            ['auto', 'manual'],
                        )}
                        value={stringValue(config.exposure, 'mode', 'auto')}
                        onChange={(value) =>
                            onSectionChange('exposure', 'mode', value)
                        }
                    />
                    <OptionField
                        label="防闪烁"
                        capability={optionCapability(
                            capabilities.exposure.options,
                            'anti_flicker',
                            ['50hz', '60hz', 'off'],
                        )}
                        value={stringValue(
                            config.exposure,
                            'anti_flicker',
                            '50hz',
                        )}
                        onChange={(value) =>
                            onSectionChange('exposure', 'anti_flicker', value)
                        }
                    />
                    <OptionField
                        label="曝光时间"
                        capability={optionCapability(
                            capabilities.exposure.options,
                            'exposure_time',
                            ['auto', '1/25', '1/30', '1/50', '1/100', '1/250'],
                        )}
                        value={stringValue(
                            config.exposure,
                            'exposure_time',
                            'auto',
                        )}
                        onChange={(value) =>
                            onSectionChange('exposure', 'exposure_time', value)
                        }
                    />
                    <OptionField
                        label="增益"
                        capability={optionCapability(
                            capabilities.exposure.options,
                            'gain',
                            ['auto', 'low', 'medium', 'high'],
                        )}
                        value={stringValue(config.exposure, 'gain', 'auto')}
                        onChange={(value) =>
                            onSectionChange('exposure', 'gain', value)
                        }
                    />
                    <RangeField
                        label="曝光补偿"
                        capability={
                            numericCapability(
                                capabilities.exposure.ranges,
                                'compensation',
                            ) || { min: 0, max: 100, default: 50 }
                        }
                        value={numberValue(config.exposure, 'compensation', 50)}
                        onChange={(value) =>
                            onSectionChange('exposure', 'compensation', value)
                        }
                    />
                    {supportsOptionValue(
                        capabilities.exposure.options,
                        'slow_shutter',
                        'true',
                    ) && (
                        <FormField label="慢快门">
                            <input
                                type="checkbox"
                                checked={boolValue(
                                    config.exposure,
                                    'slow_shutter',
                                    false,
                                )}
                                onChange={(e) =>
                                    onSectionChange(
                                        'exposure',
                                        'slow_shutter',
                                        e.target.checked,
                                    )
                                }
                            />
                        </FormField>
                    )}
                    {capabilities.exposure.options.max_exposure_time && (
                        <OptionField
                            label="最长曝光"
                            capability={
                                capabilities.exposure.options.max_exposure_time
                            }
                            value={stringValue(
                                config.exposure,
                                'max_exposure_time',
                                '1/30',
                            )}
                            onChange={(value) =>
                                onSectionChange(
                                    'exposure',
                                    'max_exposure_time',
                                    value,
                                )
                            }
                        />
                    )}
                </div>
            </details>

            <details className="image-advanced-section">
                <summary>白平衡</summary>
                <div className="form-grid image-settings-grid image-detail-grid">
                    <OptionField
                        label="白平衡模式"
                        capability={optionCapability(
                            capabilities.white_balance.options,
                            'mode',
                            ['auto', 'manual', 'indoor', 'outdoor'],
                        )}
                        value={stringValue(
                            config.white_balance,
                            'mode',
                            'auto',
                        )}
                        onChange={(value) =>
                            onSectionChange('white_balance', 'mode', value)
                        }
                    />
                    <RangeField
                        label="红色增益"
                        capability={
                            numericCapability(
                                capabilities.white_balance.ranges,
                                'red_gain',
                            ) || { min: 0, max: 100, default: 50 }
                        }
                        value={numberValue(
                            config.white_balance,
                            'red_gain',
                            50,
                        )}
                        onChange={(value) =>
                            onSectionChange('white_balance', 'red_gain', value)
                        }
                    />
                    <RangeField
                        label="蓝色增益"
                        capability={
                            numericCapability(
                                capabilities.white_balance.ranges,
                                'blue_gain',
                            ) || { min: 0, max: 100, default: 50 }
                        }
                        value={numberValue(
                            config.white_balance,
                            'blue_gain',
                            45,
                        )}
                        onChange={(value) =>
                            onSectionChange('white_balance', 'blue_gain', value)
                        }
                    />
                </div>
            </details>

            <details className="image-advanced-section">
                <summary>图像增强</summary>
                <div className="form-grid image-settings-grid image-detail-grid">
                    <RangeField
                        label="2D 降噪"
                        capability={
                            numericCapability(
                                capabilities.enhancement.ranges,
                                'denoise_2d',
                            ) || { min: 0, max: 100, default: 60 }
                        }
                        value={numberValue(
                            config.enhancement,
                            'denoise_2d',
                            60,
                        )}
                        onChange={(value) =>
                            onSectionChange('enhancement', 'denoise_2d', value)
                        }
                    />
                    <RangeField
                        label="3D 降噪"
                        capability={
                            numericCapability(
                                capabilities.enhancement.ranges,
                                'denoise_3d',
                            ) || { min: 0, max: 100, default: 52 }
                        }
                        value={numberValue(
                            config.enhancement,
                            'denoise_3d',
                            52,
                        )}
                        onChange={(value) =>
                            onSectionChange('enhancement', 'denoise_3d', value)
                        }
                    />
                    <RangeField
                        label="Gamma"
                        capability={
                            numericCapability(
                                capabilities.enhancement.ranges,
                                'gamma',
                            ) || { min: 0, max: 100, default: 50 }
                        }
                        value={numberValue(config.enhancement, 'gamma', 50)}
                        onChange={(value) =>
                            onSectionChange('enhancement', 'gamma', value)
                        }
                    />
                    {supportsOptionValue(
                        capabilities.enhancement.options,
                        'defog',
                        'true',
                    ) && (
                        <FormField label="透雾">
                            <input
                                type="checkbox"
                                checked={boolValue(
                                    config.enhancement,
                                    'defog',
                                    false,
                                )}
                                onChange={(e) =>
                                    onSectionChange(
                                        'enhancement',
                                        'defog',
                                        e.target.checked,
                                    )
                                }
                            />
                        </FormField>
                    )}
                </div>
            </details>
        </>
    );
}
