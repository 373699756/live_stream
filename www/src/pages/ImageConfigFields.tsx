import type {
    NumericControlCapability,
    OptionControlCapability,
} from '../api/types';
import { FormField } from '../components/FormField';

const optionLabels: Record<string, string> = {
    auto: '自动',
    manual: '手动',
    '50hz': '50Hz',
    '60hz': '60Hz',
    off: '关闭',
    low: '低',
    medium: '中',
    high: '高',
    indoor: '室内',
    outdoor: '室外',
    drc: '动态范围压缩',
    color: '彩色',
    black_white: '黑白',
};

export const basicImageItems = [
    ['brightness', '亮度'],
    ['contrast', '对比度'],
    ['saturation', '饱和度'],
    ['sharpness', '锐度'],
    ['hue', '色调'],
] as const;

export function optionLabel(value: string): string {
    return optionLabels[value] || value;
}

export function numericCapability(
    controls: Record<string, NumericControlCapability>,
    key: string,
): NumericControlCapability | undefined {
    return controls[key];
}

export function optionCapability(
    controls: Record<string, OptionControlCapability>,
    key: string,
    fallback: string[],
): OptionControlCapability {
    return controls[key] || { values: fallback, default: fallback[0] || '' };
}

export function RangeField({
    label,
    capability,
    value,
    onChange,
}: {
    label: string;
    capability: NumericControlCapability;
    value: number;
    onChange: (value: number) => void;
}) {
    return (
        <FormField label={label}>
            <input
                type="range"
                min={capability.min}
                max={capability.max}
                value={value}
                onChange={(e) => onChange(Number(e.target.value))}
            />
            <span className="range-value">{value}</span>
        </FormField>
    );
}

export function OptionField({
    label,
    capability,
    value,
    onChange,
}: {
    label: string;
    capability: OptionControlCapability;
    value: string;
    onChange: (value: string) => void;
}) {
    return (
        <FormField label={label}>
            <select value={value} onChange={(e) => onChange(e.target.value)}>
                {capability.values.map((item) => (
                    <option value={item} key={item}>
                        {optionLabel(item)}
                    </option>
                ))}
            </select>
        </FormField>
    );
}
