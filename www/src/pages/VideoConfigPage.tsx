import { useEffect, useRef, useState } from 'react';
import { saveVideoConfig } from '../api/video';
import { isStreamSupported } from '../api/resolution';
import { cloneDefaultConfig } from '../api/configDefaults';
import { mockVideoConfig } from '../api/mockVideo';
import type {
  StreamName,
  VideoStreamConfig,
} from '../api/types';
import { VideoPreview } from '../components/VideoPreview';
import { useVideoConfig } from '../hooks/useVideoConfig';
import { VideoStreamForm } from './VideoStreamForm';

export function VideoConfigPage() {
  const [active, setActive] = useState<StreamName>('sub');
  const {
    config,
    setConfig,
    capabilities,
    statuses,
    playbackUrls,
    reloadConfig,
    refreshStatuses,
    loading,
    error,
  } = useVideoConfig(active);
  const [saved, setSaved] = useState<string>('');
  const [saving, setSaving] = useState(false);
  const [previewEnabled, setPreviewEnabled] = useState(true);
  const refreshTimerRef = useRef(0);
  const previewTimerRef = useRef(0);

  useEffect(() => {
    return () => {
      window.clearTimeout(refreshTimerRef.current);
      window.clearTimeout(previewTimerRef.current);
    };
  }, []);

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
    bitrate_kbps: config.streams[status.stream].bitrate_kbps,
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
    window.clearTimeout(refreshTimerRef.current);
    window.clearTimeout(previewTimerRef.current);
    try {
      await saveVideoConfig(config);
      setSaved('已提交保存');
      await refreshStatuses();
      refreshTimerRef.current = window.setTimeout(() => void refreshStatuses(), 2500);
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : '保存失败';
      try {
        await reloadConfig();
        await refreshStatuses();
        setSaved(`保存失败，已恢复当前生效配置：${message}`);
      } catch {
        setSaved(`保存失败：${message}`);
      }
    } finally {
      setSaving(false);
      previewTimerRef.current = window.setTimeout(() => setPreviewEnabled(true), 2500);
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
        <VideoStreamForm
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
        playbackUrls={playbackUrls}
        onStreamChange={setActive}
        enabled={previewEnabled}
      />
    </div>
  );
}
