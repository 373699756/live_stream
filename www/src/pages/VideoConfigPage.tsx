import { useState } from 'react';
import { saveVideoConfig } from '../api/video';
import {
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

function StreamForm({
  stream,
  capabilities,
  onChange,
}: {
  stream: VideoStreamConfig;
  capabilities: VideoStreamCapabilities;
  onChange: (stream: VideoStreamConfig) => void;
}) {
  const patch = (value: Partial<VideoStreamConfig>) => onChange({ ...stream, ...value });
  const available = capabilities.available !== false;
  const supported = isStreamSupported(stream, capabilities);
  const supportedResolution = capabilities.resolutions.some((item) => resolutionValue(item) === stream.resolution);
  return (
    <div className="form-grid form-grid-single">
      <FormField label="启用">
        <input
          type="checkbox"
          disabled={!available}
          checked={stream.enabled && available}
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
      <FormField label="码率控制">
        <select
          disabled={!available}
          value={stream.rate_control}
          onChange={(e) => patch({ rate_control: e.target.value as VideoStreamConfig['rate_control'] })}
        >
          {capabilities.rate_control.map((mode) => (
            <option value={mode} key={mode}>
              {mode.toUpperCase()}
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
      {!available && <div className="save-hint">当前固件未启用该码流管线。</div>}
      {available && !supported && <div className="save-hint">当前参数不在设备能力范围内，请修正后保存。</div>}
    </div>
  );
}

export function VideoConfigPage() {
  const { config, setConfig, capabilities, statuses } = useVideoConfig();
  const [active, setActive] = useState<StreamName>('sub');
  const [saved, setSaved] = useState<string>('');

  if (!config) {
    return <div className="panel">加载视频配置...</div>;
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
          <button type="button" className={active === 'main' ? 'active' : ''} onClick={() => setActive('main')}>主码流</button>
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
            disabled={!allSupported}
            onClick={() => void saveVideoConfig(config).then((ok) => setSaved(ok ? '已提交保存' : '后端未连接，已保留本地修改'))}
          >
            保存
          </button>
        </div>
        {!activeSupported && <div className="save-hint">当前码流包含设备不支持的参数。</div>}
        {saved && <div className="save-hint">{saved}</div>}
      </section>
      <VideoPreview stream={active} statuses={previewStatuses} onStreamChange={setActive} />
    </div>
  );
}
