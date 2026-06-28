export type StreamName = 'main' | 'sub';
export type Resolution = string;
export type VideoCodecName = 'h264' | 'h265' | 'jpeg' | 'mjpeg';
export type GopMode = 'normal_p' | 'dual_p' | 'smart_p';
export type ImageStrategyMode = 'balanced' | 'low_noise' | 'detail';

export interface NumberRange {
    min: number;
    max: number;
}
