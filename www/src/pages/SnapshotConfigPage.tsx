import { useEffect, useState } from 'react';
import { api } from '../api/client';
import { cloneDefaultConfig, mockSnapshotConfig } from '../api/mock';
import type { SnapshotConfig, StreamName, StreamStatus } from '../api/types';
import { FormField } from '../components/FormField';
import { VideoPreview } from '../components/VideoPreview';

export function SnapshotConfigPage() {
  const [config, setConfig] = useState<SnapshotConfig | null>(null);
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);
  const [previewStream, setPreviewStream] = useState<StreamName>('main');
  const [saved, setSaved] = useState('');

  useEffect(() => {
    void api.getSnapshotConfig().then(setConfig);
    void api.getStreamStatus().then(setStatuses);
  }, []);

  if (!config) {
    return <div className="panel">加载抓图配置...</div>;
  }

  const resetDefault = () => {
    setConfig(cloneDefaultConfig(mockSnapshotConfig));
    setSaved('已恢复默认值，保存后生效');
  };

  return (
    <div className="config-preview-layout">
      <section className="panel settings-column">
        <div className="page-heading">
          <div>
            <h2>抓图参数</h2>
            <p>配置 HTTP/ONVIF 抓图入口参数，实际 JPEG 生成由后端 snapshot_service 完成。</p>
          </div>
        </div>

        <div className="form-grid form-grid-single">
          <FormField label="启用抓图">
            <input
              type="checkbox"
              checked={config.enabled}
              onChange={(event) => setConfig({ ...config, enabled: event.target.checked })}
            />
          </FormField>
          <FormField label="JPEG 质量">
            <input
              type="number"
              min="1"
              max="100"
              value={config.jpeg_quality}
              onChange={(event) => setConfig({ ...config, jpeg_quality: Number(event.target.value) })}
            />
          </FormField>
          <FormField label="超时 ms">
            <input
              type="number"
              min="100"
              value={config.timeout_ms}
              onChange={(event) => setConfig({ ...config, timeout_ms: Number(event.target.value) })}
            />
          </FormField>
          <FormField label="主码流路径">
            <input
              value={config.main_path}
              onChange={(event) => setConfig({ ...config, main_path: event.target.value })}
            />
          </FormField>
          <FormField label="子码流路径">
            <input
              value={config.sub_path}
              onChange={(event) => setConfig({ ...config, sub_path: event.target.value })}
            />
          </FormField>
        </div>

        <div className="snapshot-links">
          <a className="button-like" href={config.main_path} target="_blank" rel="noreferrer">
            预览主码流抓图
          </a>
          <a className="button-like" href={config.sub_path} target="_blank" rel="noreferrer">
            预览子码流抓图
          </a>
        </div>
        <div className="form-actions">
          <button type="button" onClick={resetDefault}>恢复默认</button>
          <button
            type="button"
            className="primary"
            onClick={() =>
              void api
                .saveSnapshotConfig(config)
                .then((ok) => setSaved(ok ? '已提交保存' : '后端未连接，已保留本地修改'))
            }
          >
            保存
          </button>
        </div>
        {saved && <div className="save-hint">{saved}</div>}
      </section>
      <VideoPreview stream={previewStream} statuses={statuses} onStreamChange={setPreviewStream} />
    </div>
  );
}
