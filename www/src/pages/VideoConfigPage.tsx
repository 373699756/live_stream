import { useState } from 'react';
import { saveVideoConfig } from '../api/video';
import {
  codecSupportsSmartP,
  isStreamSupported,
  resolutionLabel,
  resolutionValue,
} from '../api/resolution';
import { cloneDefaultConfig, mockVideoConfig } from '../api/mock';
import type {
  StreamName,
  VideoStreamCapabilities,
  VideoStreamConfig,
} from '../api/types';
import { FormField } from '../components/FormField';
import { VideoPreview } from '../components/VideoPreview';
import { useVideoConfig } from '../hooks/useVideoConfig';

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

function StreamForm({
  stream,
  capabilities,
  onChange,
}: {
  stream: VideoStreamConfig;
  capabilities: VideoStreamCapabilities;
  onChange: (stream: VideoStreamConfig) => void;
}) {
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
  const supportedResolution = capabilities.resolutions.some((item) => resolutionValue(item) === stream.resolution);
  const smartCodecSupported = codecSupportsSmartP(stream.codec) && capabilities.smart_codec;
  const gopModes: VideoStreamConfig['gop_mode'][] = smartCodecSupported
    ? ['normal_p', 'dual_p', 'smart_p']
    : ['normal_p', 'dual_p'];
  const selectedGopMode = stream.smart_codec ? 'smart_p' : stream.gop_mode;
  const currentGopMode = gopModes.includes(selectedGopMode) ? selectedGopMode : 'normal_p';
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
      {!available && <div className="save-hint">当前固件未启用该码流管线。</div>}
      {available && !supported && <div className="save-hint">当前参数不在设备能力范围内，请修正后保存。</div>}
    </div>
  );
}

export function VideoConfigPage() {
  const {
    config,
    setConfig,
    capabilities,
    statuses,
    loading,
    error,
  } = useVideoConfig();
  const [active, setActive] = useState<StreamName>('sub');
  const [saved, setSaved] = useState<string>('');
  const [saving, setSaving] = useState(false);
  const [previewEnabled, setPreviewEnabled] = useState(true);

  if (loading) {
    return <div className="panel">加载视频配置...</div>;
  }
  if (!config) {
    return <div className="panel">视频配置加载失败：{error || '无可用配置'}</div>;
  }

  const updateStream = (name: StreamName, stream: VideoStreamConfig) => {
    setConfig({ ...config, streams: { ...config.streams, [name]: stream } });
  };
  const previewStatuses = statuses.map((status) => ({
    ...status,
    resolution: config.streams[status.stream].resolution,
    fps: config.streams[status.stream].fps,
    bitrateKbps: config.streams[status.stream].bitrate_kbps,
  }));
  const resetDefault = () => {
    setConfig(cloneDefaultConfig(mockVideoConfig));
    setSaved('已恢复默认值，保存后生效');
  };
  const activeCapabilities = capabilities.streams[active];
  const activeSupported = isStreamSupported(config.streams[active], activeCapabilities);
  const allSupported =
    (capabilities.streams.main.available === false ||
      isStreamSupported(config.streams.main, capabilities.streams.main)) &&
    (capabilities.streams.sub.available === false ||
      isStreamSupported(config.streams.sub, capabilities.streams.sub));
  const saveConfig = async () => {
    setSaving(true);
    setPreviewEnabled(false);
    try {
      await saveVideoConfig(config);
      setSaved('已提交保存');
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : '保存失败';
      setSaved(`保存失败：${message}`);
    } finally {
      setSaving(false);
      window.setTimeout(() => setPreviewEnabled(true), 600);
    }
  };

  return (
    <div className="config-preview-layout">
      <section className="panel settings-column">
        <div className="page-heading">
          <div>
            <h2>视频参数</h2>
            <p>主码流用于高清预览和协议输出，子码流用于低码率预览。</p>
          </div>
        </div>
        <div className="tabs">
          <button
            type="button"
            className={active === 'main' ? 'active' : ''}
            onClick={() => setActive('main')}
          >
            主码流
          </button>
          <button
            type="button"
            className={active === 'sub' ? 'active' : ''}
            disabled={capabilities.streams.sub.available === false}
            onClick={() => setActive('sub')}
          >
            子码流
          </button>
        </div>
        <StreamForm
          stream={config.streams[active]}
          capabilities={capabilities.streams[active]}
          onChange={(stream) => updateStream(active, stream)}
        />
        <div className="form-actions">
          <button type="button" onClick={resetDefault}>恢复默认</button>
          <button
            type="button"
            className="primary"
            disabled={!allSupported || saving}
            onClick={() => void saveConfig()}
          >
            {saving ? '保存中' : '保存'}
          </button>
        </div>
        {!activeSupported && <div className="save-hint">当前码流包含设备不支持的参数。</div>}
        {saved && <div className="save-hint">{saved}</div>}
        {error && <div className="status-note error-note">{error}</div>}
      </section>
      <VideoPreview
        stream={active}
        statuses={previewStatuses}
        onStreamChange={setActive}
        enabled={previewEnabled}
      />
    </div>
  );
}
