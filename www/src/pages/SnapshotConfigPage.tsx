import { useSnapshotConfig } from '../hooks/useSnapshotConfig';
import { FormField } from '../components/FormField';

export function SnapshotConfigPage() {
  const {
    config,
    setConfig,
    save,
    reset,
    savedMsg,
    loading,
    saving,
    error,
  } = useSnapshotConfig();

  if (loading) {
    return <div className="panel">加载抓图配置...</div>;
  }
  if (!config) {
    return <div className="panel">抓图配置加载失败：{error || '无可用配置'}</div>;
  }

  return (
    <div className="config-preview-layout">
      <section className="panel settings-column">
        <div className="page-heading">
          <div>
            <h2>抓图参数</h2>
            <p>配置 HTTP/ONVIF 抓图入口参数，实际 JPEG 生成由后端 snapshot 完成。</p>
          </div>
        </div>

        <div className="form-grid form-grid-single">
          <FormField label="启用抓图">
            <input
              type="checkbox"
              checked={config.enabled}
              onChange={(event) =>
                setConfig({ ...config, enabled: event.target.checked })
              }
            />
          </FormField>
          <FormField label="JPEG 质量">
            <input
              type="number"
              min="1"
              max="100"
              value={config.jpeg_quality}
              onChange={(event) =>
                setConfig({ ...config, jpeg_quality: Number(event.target.value) })
              }
            />
          </FormField>
          <FormField label="超时 ms">
            <input
              type="number"
              min="100"
              value={config.timeout_ms}
              onChange={(event) =>
                setConfig({ ...config, timeout_ms: Number(event.target.value) })
              }
            />
          </FormField>
          <FormField label="主码流路径">
            <input
              value={config.main_path}
              onChange={(event) =>
                setConfig({ ...config, main_path: event.target.value })
              }
            />
          </FormField>
          <FormField label="子码流路径">
            <input
              value={config.sub_path}
              onChange={(event) =>
                setConfig({ ...config, sub_path: event.target.value })
              }
            />
          </FormField>
        </div>

        <div className="form-actions">
          <button type="button" onClick={reset}>恢复默认</button>
          <button
            type="button"
            className="primary"
            disabled={saving}
            onClick={() => void save()}
          >
            {saving ? '保存中' : '保存'}
          </button>
        </div>
        {savedMsg && <div className="save-hint">{savedMsg}</div>}
        {error && <div className="status-note error-note">{error}</div>}
      </section>
    </div>
  );
}
