import {
  codecSupportsSmartP,
  isStreamSupported,
  resolutionLabel,
  resolutionValue,
} from '../api/resolution';
import type {
  VideoRoiRegion,
  VideoStreamCapabilities,
  VideoStreamConfig,
} from '../api/types';
import { FormField } from '../components/FormField';

const codecLabel = (codec: string) => {
  if (codec === 'h264') return 'H.264';
  if (codec === 'h265') return 'H.265';
  if (codec === 'jpeg') return 'JPEG';
  if (codec === 'mjpeg') return 'MJPEG';
  return codec;
};

const gopModeLabel = (mode: VideoStreamConfig['gop_mode']) => {
  if (mode === 'normal_p') return 'Normal P';
  if (mode === 'dual_p') return 'Dual P';
  if (mode === 'smart_p') return 'Smart P';
  return mode;
};

const rateControlLabel = (mode: VideoStreamConfig['rate_control']) => {
  if (mode === 'cbr') return 'CBR';
  if (mode === 'vbr') return 'VBR';
  if (mode === 'fixqp') return 'Fix QP';
  return String(mode).toUpperCase();
};

function parseResolutionSize(resolution: string) {
  const match = /^(\d+)x(\d+)$/.exec(resolution);
  if (!match) {
    return { width: 0, height: 0 };
  }
  return { width: Number(match[1]), height: Number(match[2]) };
}

function defaultRoiRegion(index: number, resolution: string): VideoRoiRegion {
  const size = parseResolutionSize(resolution);
  const width = Math.max(16, Math.floor((size.width || 640) / 4));
  const height = Math.max(16, Math.floor((size.height || 360) / 4));
  return {
    enabled: true,
    x: Math.max(0, Math.floor(index * 16)),
    y: Math.max(0, Math.floor(index * 16)),
    width,
    height,
    qp: -6,
    absolute_qp: false,
  };
}

function clampNumber(value: number, min: number, max: number) {
  if (!Number.isFinite(value)) return min;
  return Math.min(Math.max(Math.round(value), min), max);
}

interface VideoStreamFormProps {
  stream: VideoStreamConfig;
  capabilities: VideoStreamCapabilities;
  onChange: (stream: VideoStreamConfig) => void;
}

export function VideoStreamForm({
  stream,
  capabilities,
  onChange,
}: VideoStreamFormProps) {
  const patch = (value: Partial<VideoStreamConfig>) => {
    const next = { ...stream, ...value };
    if (!codecSupportsSmartP(next.codec) || !capabilities.smart_codec) {
      next.smart_codec = false;
      if (next.gop_mode === 'smart_p') {
        next.gop_mode = 'normal_p';
      }
    }
    onChange(next);
  };
  const available = capabilities.available !== false;
  const supported = isStreamSupported(stream, capabilities);
  const supportedResolution = capabilities.resolutions.some((item) =>
    resolutionValue(item) === stream.resolution);
  const smartCodecSupported =
    codecSupportsSmartP(stream.codec) && capabilities.smart_codec;
  const gopModes: VideoStreamConfig['gop_mode'][] = smartCodecSupported
    ? ['normal_p', 'dual_p', 'smart_p']
    : ['normal_p', 'dual_p'];
  const selectedGopMode = stream.smart_codec ? 'smart_p' : stream.gop_mode;
  const currentGopMode = gopModes.includes(selectedGopMode)
    ? selectedGopMode
    : 'normal_p';
  const smartCodecState = smartCodecSupported
    ? currentGopMode === 'smart_p'
      ? 'SmartP 已启用'
      : 'SmartP 可选'
    : 'SmartP 不可用';
  const rateControlHint =
    stream.rate_control === 'vbr'
      ? currentGopMode === 'smart_p'
        ? 'VBR 使用码率上限，SmartP 降低静态画面的参考帧成本。'
        : 'VBR 使用码率上限，复杂画面放宽，静态画面收敛。'
      : stream.rate_control === 'fixqp'
        ? 'Fix QP 适合稳定测试或固定量化场景。'
        : 'CBR 适合固定带宽链路，输出更平稳。';
  const smartCodecHint = smartCodecSupported
    ? currentGopMode === 'smart_p'
      ? 'SmartP 仅对 H.264/H.265 生效，保存后后端按智能 GOP 应用。'
      : '选择 Smart P 即启用 Smart H.264/H.265；Normal/Dual P 会关闭智能编码。'
    : '当前编码或固件能力不支持 SmartP。';
  const roiSupported =
    Boolean(capabilities.roi_supported) &&
    (stream.codec === 'h264' || stream.codec === 'h265');
  const maxRoiRegions = capabilities.max_roi_regions || 0;
  const roiRegions = stream.roi?.regions ?? [];
  const streamSize = parseResolutionSize(stream.resolution);
  const patchRoi = (value: Partial<VideoStreamConfig['roi']>) => {
    patch({
      roi: {
        enabled: stream.roi?.enabled ?? false,
        regions: roiRegions,
        ...value,
      },
    });
  };
  const addRoiRegion = () => {
    if (!roiSupported || roiRegions.length >= maxRoiRegions) return;
    patchRoi({
      regions: [
        ...roiRegions,
        defaultRoiRegion(roiRegions.length, stream.resolution),
      ],
    });
  };
  const updateRoiRegion = (
    index: number,
    value: Partial<VideoRoiRegion>,
  ) => {
    const nextRegions = roiRegions.map((region, regionIndex) => {
      if (regionIndex !== index) return region;
      const next = { ...region, ...value };
      next.x = clampNumber(next.x, 0, Math.max(0, streamSize.width - 1));
      next.y = clampNumber(next.y, 0, Math.max(0, streamSize.height - 1));
      next.width = clampNumber(next.width, 1, Math.max(1, streamSize.width - next.x));
      next.height = clampNumber(next.height, 1, Math.max(1, streamSize.height - next.y));
      next.qp = clampNumber(next.qp, -51, 51);
      return next;
    });
    patchRoi({ regions: nextRegions });
  };
  const removeRoiRegion = (index: number) => {
    patchRoi({ regions: roiRegions.filter((_, regionIndex) => regionIndex !== index) });
  };

  return (
    <div className="form-grid form-grid-single">
      <FormField label="启用">
        <input
          type="checkbox"
          disabled={!available}
          checked={stream.enabled}
          onChange={(e) => patch({ enabled: e.target.checked })}
        />
      </FormField>
      <FormField label="编码格式">
        <select
          disabled={!available}
          value={stream.codec}
          onChange={(e) => patch({ codec: e.target.value as VideoStreamConfig['codec'] })}
        >
          {capabilities.codecs.map((item) => (
            <option value={item.codec} key={item.codec}>
              {codecLabel(item.codec)}
            </option>
          ))}
        </select>
      </FormField>
      <FormField label="分辨率">
        <select
          disabled={!available}
          value={supportedResolution ? stream.resolution : ''}
          onChange={(e) => patch({ resolution: e.target.value })}
        >
          {!supportedResolution && <option value="">当前值不受设备支持：{stream.resolution}</option>}
          {capabilities.resolutions.map((option) => (
            <option value={resolutionValue(option)} key={resolutionValue(option)}>
              {resolutionLabel(option)}
            </option>
          ))}
        </select>
      </FormField>
      <FormField label="帧率">
        <input
          type="number"
          min={capabilities.fps.min}
          max={capabilities.fps.max}
          disabled={!available}
          value={stream.fps}
          aria-invalid={stream.fps < capabilities.fps.min || stream.fps > capabilities.fps.max}
          onChange={(e) => patch({ fps: Number(e.target.value) })}
        />
      </FormField>
      <FormField label="码率 kbps">
        <input
          type="number"
          min={capabilities.bitrate_kbps.min}
          max={capabilities.bitrate_kbps.max}
          disabled={!available}
          value={stream.bitrate_kbps}
          aria-invalid={
            stream.bitrate_kbps < capabilities.bitrate_kbps.min ||
            stream.bitrate_kbps > capabilities.bitrate_kbps.max
          }
          onChange={(e) => patch({ bitrate_kbps: Number(e.target.value) })}
        />
      </FormField>
      <div className="stream-advanced-block">
        <div className="stream-advanced-block-title">码率控制</div>
        <div className="stream-control-segmented">
          {capabilities.rate_control.map((mode) => (
            <button
              type="button"
              key={mode}
              className={stream.rate_control === mode ? 'active' : ''}
              disabled={!available}
              onClick={() => patch({ rate_control: mode })}
            >
              {rateControlLabel(mode)}
            </button>
          ))}
        </div>
        <div className="stream-advanced-hint">{rateControlHint}</div>
      </div>
      <div className="stream-advanced-block">
        <div className="stream-advanced-block-title">Smart H.264/H.265</div>
        <div className="stream-state-line">
          <span>{codecLabel(stream.codec)}</span>
          <span>{smartCodecState}</span>
        </div>
        <FormField label="GOP模式">
          <select
            disabled={!available}
            value={currentGopMode}
            onChange={(e) => {
              const mode = e.target.value as VideoStreamConfig['gop_mode'];
              patch({ gop_mode: mode, smart_codec: mode === 'smart_p' });
            }}
          >
            {gopModes.map((mode) => (
              <option value={mode} key={mode}>
                {gopModeLabel(mode)}
              </option>
            ))}
          </select>
        </FormField>
        <FormField label="GOP">
          <input
            type="number"
            min={capabilities.gop.min}
            max={capabilities.gop.max}
            disabled={!available}
            value={stream.gop}
            aria-invalid={stream.gop < capabilities.gop.min || stream.gop > capabilities.gop.max}
            onChange={(e) => patch({ gop: Number(e.target.value) })}
          />
        </FormField>
        <div className="stream-advanced-hint">{smartCodecHint}</div>
      </div>
      <div className="stream-advanced-block">
        <div className="stream-advanced-block-title">ROI 编码</div>
        <FormField label="启用">
          <input
            type="checkbox"
            disabled={!available || !roiSupported}
            checked={Boolean(stream.roi?.enabled)}
            onChange={(e) => patchRoi({ enabled: e.target.checked })}
          />
        </FormField>
        <div className="stream-state-line">
          <span>{roiRegions.length} / {maxRoiRegions || 0} 区域</span>
          <button
            type="button"
            disabled={!available || !roiSupported || roiRegions.length >= maxRoiRegions}
            onClick={addRoiRegion}
          >
            添加区域
          </button>
        </div>
        {roiRegions.length > 0 && (
          <div className="roi-region-table">
            <div className="roi-region-head">
              <span>开关</span>
              <span>X</span>
              <span>Y</span>
              <span>宽</span>
              <span>高</span>
              <span>QP</span>
              <span>模式</span>
              <span>操作</span>
            </div>
            {roiRegions.map((region, index) => (
              <div className="roi-region-row" key={`${index}-${region.x}-${region.y}`}>
                <input
                  type="checkbox"
                  disabled={!available || !roiSupported}
                  checked={region.enabled}
                  onChange={(e) => updateRoiRegion(index, { enabled: e.target.checked })}
                />
                <input
                  type="number"
                  min={0}
                  max={Math.max(0, streamSize.width - 1)}
                  disabled={!available || !roiSupported}
                  value={region.x}
                  onChange={(e) => updateRoiRegion(index, { x: Number(e.target.value) })}
                />
                <input
                  type="number"
                  min={0}
                  max={Math.max(0, streamSize.height - 1)}
                  disabled={!available || !roiSupported}
                  value={region.y}
                  onChange={(e) => updateRoiRegion(index, { y: Number(e.target.value) })}
                />
                <input
                  type="number"
                  min={1}
                  max={Math.max(1, streamSize.width - region.x)}
                  disabled={!available || !roiSupported}
                  value={region.width}
                  onChange={(e) => updateRoiRegion(index, { width: Number(e.target.value) })}
                />
                <input
                  type="number"
                  min={1}
                  max={Math.max(1, streamSize.height - region.y)}
                  disabled={!available || !roiSupported}
                  value={region.height}
                  onChange={(e) => updateRoiRegion(index, { height: Number(e.target.value) })}
                />
                <input
                  type="number"
                  min={-51}
                  max={51}
                  disabled={!available || !roiSupported}
                  value={region.qp}
                  onChange={(e) => updateRoiRegion(index, { qp: Number(e.target.value) })}
                />
                <select
                  disabled={!available || !roiSupported}
                  value={region.absolute_qp ? 'absolute' : 'relative'}
                  onChange={(e) => updateRoiRegion(index, {
                    absolute_qp: e.target.value === 'absolute',
                  })}
                >
                  <option value="relative">相对</option>
                  <option value="absolute">绝对</option>
                </select>
                <button
                  type="button"
                  disabled={!available}
                  onClick={() => removeRoiRegion(index)}
                >
                  删除
                </button>
              </div>
            ))}
          </div>
        )}
        <div className="stream-advanced-hint">
          {roiSupported
            ? '相对 QP 使用负值可提升区域清晰度；画面仍保持完整输出。'
            : 'ROI 仅对支持的 H.264/H.265 编码通道生效。'}
        </div>
      </div>
      {!available && <div className="save-hint">当前固件未启用该码流管线。</div>}
      {available && !supported && <div className="save-hint">当前参数不在设备能力范围内，请修正后保存。</div>}
    </div>
  );
}
