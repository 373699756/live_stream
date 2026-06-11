import type { ImageCapabilities, ImageConfig, ImageInfo } from '../api/types';
import { FormField } from '../components/FormField';
import {
    basicImageItems,
    numericCapability,
    OptionField,
    RangeField,
} from './ImageConfigFields';

const tierLabels: Record<string, string> = {
    day: '日间',
    indoor: '室内',
    low_light: '弱光',
    very_low_light: '极弱光',
};

const strategyModeLabels: Record<string, string> = {
    balanced: '均衡',
    low_noise: '低噪声',
    detail: '细节优先',
};

function tierLabel(value: string): string {
    return tierLabels[value] || value || '-';
}

function strategyModeLabel(value: string): string {
    return strategyModeLabels[value] || value || '-';
}

interface ImagePrimarySettingsProps {
    capabilities: ImageCapabilities;
    config: ImageConfig;
    onBasicChange: (key: string, value: number) => void;
    onStrategyEnabledChange: (enabled: boolean) => void;
    onStrategyModeChange: (mode: string) => void;
    strategyEnabled: boolean;
    imageInfo: ImageInfo;
}

export function ImagePrimarySettings({
    capabilities,
    config,
    onBasicChange,
    onStrategyEnabledChange,
    onStrategyModeChange,
    strategyEnabled,
    imageInfo,
}: ImagePrimarySettingsProps) {
    return (
        <div className="form-grid image-settings-grid image-primary-grid">
            <div className="form-section-title">自动画质策略</div>
            <FormField label="自动策略">
                <input
                    type="checkbox"
                    checked={strategyEnabled}
                    onChange={(event) =>
                        onStrategyEnabledChange(event.target.checked)
                    }
                />
            </FormField>
            <OptionField
                label="策略模式"
                capability={{
                    values: ['balanced', 'low_noise', 'detail'],
                    default: 'low_noise',
                }}
                value={config.strategy?.mode || 'low_noise'}
                onChange={onStrategyModeChange}
            />
            <div className="image-info image-info-strip">
                <span>
                    <strong>状态</strong>
                    {imageInfo.active ? '运行中' : '未运行'}
                </span>
                <span>
                    <strong>场景</strong>
                    {tierLabel(imageInfo.tier)}
                </span>
                <span>
                    <strong>模式</strong>
                    {strategyModeLabel(imageInfo.mode)}
                </span>
                <span>
                    <strong>ISO</strong>
                    {imageInfo.exposure_valid ? imageInfo.iso : '-'}
                </span>
                <span>
                    <strong>曝光</strong>
                    {imageInfo.exposure_valid
                        ? `${imageInfo.exposure_time_us} us`
                        : '-'}
                </span>
            </div>
            <details className="image-info-details">
                <summary>更多运行值</summary>
                <div className="image-info image-info-grid">
                    <span>
                        <strong>饱和</strong>
                        {imageInfo.saturation}
                    </span>
                    <span>
                        <strong>锐度</strong>
                        {imageInfo.sharpness}
                    </span>
                    <span>
                        <strong>2DNR</strong>
                        {imageInfo.denoise_2d}
                    </span>
                    <span>
                        <strong>3DNR</strong>
                        {imageInfo.denoise_3d}
                    </span>
                    <span>
                        <strong>Gamma</strong>
                        {imageInfo.gamma}
                    </span>
                </div>
            </details>

            <div className="form-section-title">基础画质</div>
            {basicImageItems.map(([key, label]) => {
                const capability = numericCapability(capabilities.basic, key);
                return capability &&
                    capability.live_update_supported !== false ? (
                    <RangeField
                        label={label}
                        capability={capability}
                        value={config.basic[key] ?? capability.default}
                        onChange={(value) => onBasicChange(key, value)}
                        key={key}
                    />
                ) : null;
            })}
        </div>
    );
}
