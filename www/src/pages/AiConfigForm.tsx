import type { AiModelConfig } from '../api/types';

interface AiConfigFormProps {
  config: AiModelConfig;
  saving: boolean;
  saveMessage: string;
  onChange: (config: AiModelConfig) => void;
  onRestore: () => void;
  onSave: () => void;
}

export function AiConfigForm({
  config,
  saving,
  saveMessage,
  onChange,
  onRestore,
  onSave,
}: AiConfigFormProps) {
  return (
    <div className="ai-config-grid">
      <label className="form-field">
        <span className="form-label">启用</span>
        <span className="form-control">
          <input
            checked={config.enabled}
            type="checkbox"
            onChange={(event) =>
              onChange({ ...config, enabled: event.target.checked })
            }
          />
        </span>
      </label>
      <label className="form-field">
        <span className="form-label">后端</span>
        <span className="form-control">
          <select
            value={config.backend}
            onChange={(event) =>
              onChange({
                ...config,
                backend: event.target.value as AiModelConfig['backend'],
              })
            }
          >
            <option value="hisi3516dv300_nnie">HiSilicon NNIE/IVS</option>
            <option value="host_stub">Host stub</option>
          </select>
        </span>
      </label>
      <label className="form-field">
        <span className="form-label">任务</span>
        <span className="form-control">
          <select
            value={config.task}
            onChange={(event) =>
              onChange({
                ...config,
                task: event.target.value as AiModelConfig['task'],
              })
            }
          >
            <option value="object_detection">目标检测</option>
            <option value="motion_classification">移动侦测</option>
            <option value="occlusion_detection">遮挡检测</option>
          </select>
        </span>
      </label>
      <label className="form-field">
        <span className="form-label">码流</span>
        <span className="form-control">
          <select
            value={config.stream}
            onChange={(event) =>
              onChange({
                ...config,
                stream: event.target.value as AiModelConfig['stream'],
              })
            }
          >
            <option value="sub">子码流</option>
            <option value="main">主码流</option>
          </select>
        </span>
      </label>
      <label className="form-field ai-config-path">
        <span className="form-label">模型</span>
        <span className="form-control">
          <input
            value={config.model_path}
            onChange={(event) =>
              onChange({ ...config, model_path: event.target.value })
            }
          />
        </span>
      </label>
      <label className="form-field">
        <span className="form-label">间隔 ms</span>
        <span className="form-control">
          <input
            min={100}
            step={100}
            type="number"
            value={config.inference_interval_ms}
            onChange={(event) =>
              onChange({
                ...config,
                inference_interval_ms: Number(event.target.value),
              })
            }
          />
        </span>
      </label>
      <label className="form-field">
        <span className="form-label">阈值</span>
        <span className="form-control">
          <input
            max={1}
            min={0}
            step={0.05}
            type="number"
            value={config.confidence_threshold}
            onChange={(event) =>
              onChange({
                ...config,
                confidence_threshold: Number(event.target.value),
              })
            }
          />
        </span>
      </label>
      <label className="form-field">
        <span className="form-label">结果数</span>
        <span className="form-control">
          <input
            min={1}
            step={1}
            type="number"
            value={config.max_results}
            onChange={(event) =>
              onChange({ ...config, max_results: Number(event.target.value) })
            }
          />
        </span>
      </label>
      <div className="ai-config-actions">
        <button
          type="button"
          disabled={saving}
          onClick={onRestore}
        >
          恢复
        </button>
        <button
          type="button"
          className="primary"
          disabled={saving}
          onClick={onSave}
        >
          {saving ? '保存中' : '保存配置'}
        </button>
        {saveMessage ? <span>{saveMessage}</span> : null}
      </div>
    </div>
  );
}
