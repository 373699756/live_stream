import { useEffect, useRef, useState } from 'react';
import { saveAiAlarmRule } from '../../api/alarm';
import { saveAiConfig } from '../../api/ai';
import type {
  AiConfig,
  AiModelConfig,
  AiStatus,
  AlarmConfig,
  AlarmRuleConfig,
  StreamName,
} from '../../api/types';
import { StatusBadge } from '../../components/StatusBadge';
import {
  nonNegativeInteger,
  normalizeAiConfigForSave,
  normalizeAiRootConfigForSave,
} from './aiAlertFormat';
import { backendBadgeState } from './aiAlertStatus';
import { taskLabel, taskRequiresModelPath } from './aiAlertTasks';

interface AiCommonConfigPanelProps {
  status: AiStatus | null;
  alarmConfig: AlarmConfig | null;
  onSaved: () => Promise<void>;
}

export function AiCommonConfigPanel({
  status,
  alarmConfig,
  onSaved,
}: AiCommonConfigPanelProps) {
  const [draft, setDraft] = useState<AiModelConfig | null>(null);
  const [rootDraft, setRootDraft] = useState<AiConfig | null>(null);
  const [alarmRule, setAlarmRule] = useState<AlarmRuleConfig | null>(null);
  const [configDirty, setConfigDirty] = useState(false);
  const [alarmRuleDirty, setAlarmRuleDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [saveMessage, setSaveMessage] = useState('');
  const preserveSaveMessageOnStatusSync = useRef(false);

  useEffect(() => {
    if (configDirty) {
      return;
    }
    const rootConfig = status
      ? {
          ...status.config,
          tasks: status.config.tasks.map((task) => ({ ...task })),
        }
      : null;
    setRootDraft(rootConfig);
    setDraft(
      rootConfig
        ? {
            ...(rootConfig.tasks.find(
              (task) => task.task === 'perimeter_detection',
            ) ?? rootConfig.tasks[0]),
          }
        : null,
    );
    if (preserveSaveMessageOnStatusSync.current) {
      preserveSaveMessageOnStatusSync.current = false;
    } else {
      setSaveMessage('');
    }
  }, [configDirty, status]);

  useEffect(() => {
    if (alarmRuleDirty) {
      return;
    }
    setAlarmRule(
      alarmConfig
        ? {
            ...alarmConfig.ai_detection,
            regions: [...alarmConfig.ai_detection.regions],
        }
        : null,
    );
  }, [alarmConfig, alarmRuleDirty]);

  if (!draft) {
    return (
      <section className="ai-event-config">
        <div className="empty-state">加载智能配置...</div>
      </section>
    );
  }

  const modelRequired =
    draft.enabled && taskRequiresModelPath(draft.task, draft.backend);
  const modelMissing = modelRequired && draft.model_path.trim() === '';
  const updateDraft = (nextConfig: AiModelConfig) => {
    setDraft(nextConfig);
    setRootDraft((currentRoot) =>
      currentRoot
        ? {
            ...currentRoot,
            enabled: nextConfig.enabled,
            tasks: currentRoot.tasks.map((task) =>
              task.task === nextConfig.task ? nextConfig : task,
            ),
          }
        : currentRoot,
    );
    setConfigDirty(true);
    preserveSaveMessageOnStatusSync.current = false;
    setSaveMessage('');
  };
  const updateAlarmRule = (nextRule: AlarmRuleConfig | null) => {
    setAlarmRule(nextRule);
    setAlarmRuleDirty(true);
    preserveSaveMessageOnStatusSync.current = false;
    setSaveMessage('');
  };
  const saveEventConfig = () => {
    if (!rootDraft) {
      return;
    }
    const nextConfig = normalizeAiConfigForSave(draft);
    const nextRootConfig = {
      ...rootDraft,
      enabled: nextConfig.enabled,
      tasks: rootDraft.tasks.map((task) =>
        task.task === nextConfig.task ? nextConfig : task,
      ),
    };
    if (
      nextConfig.enabled &&
      taskRequiresModelPath(nextConfig.task, nextConfig.backend) &&
      !nextConfig.model_path.trim()
    ) {
      preserveSaveMessageOnStatusSync.current = false;
      setSaveMessage('保存失败：模型路径不能为空');
      return;
    }
    setSaving(true);
    preserveSaveMessageOnStatusSync.current = false;
    setSaveMessage('');
    const requests: Promise<void>[] = [
      saveAiConfig(normalizeAiRootConfigForSave(nextRootConfig)),
    ];
    if (alarmConfig && alarmRule) {
      requests.push(
        saveAiAlarmRule(alarmConfig, {
          ...alarmRule,
          min_duration_ms: nonNegativeInteger(alarmRule.min_duration_ms, 0),
        }),
      );
    }
    void Promise.all(requests)
      .then(() => {
        preserveSaveMessageOnStatusSync.current = true;
        return onSaved();
      })
      .then(() => {
        setConfigDirty(false);
        setAlarmRuleDirty(false);
        setDraft(nextConfig);
        setRootDraft(nextRootConfig);
        setSaveMessage('已保存并应用');
      })
      .catch((err: unknown) => {
        preserveSaveMessageOnStatusSync.current = false;
        setSaveMessage(err instanceof Error ? err.message : '保存失败');
      })
      .finally(() => setSaving(false));
  };

  return (
    <section className="ai-event-config">
      <div className="ai-event-config-header">
        <div>
          <h3>智能配置</h3>
          <p>四类 AI 任务可独立启停；这里保存任务参数和报警联动。</p>
        </div>
        <StatusBadge
          state={status ? backendBadgeState(status) : 'pending'}
          label={`当前任务: ${taskLabel(draft.task)}`}
        />
      </div>

      <div className="ai-event-config-grid">
        <label className="form-field">
          <span className="form-label">AI 使能</span>
          <span className="form-control">
            <input
              checked={draft.enabled}
              type="checkbox"
              onChange={(event) =>
                updateDraft({ ...draft, enabled: event.target.checked })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">报警联动</span>
          <span className="form-control">
            <input
              checked={alarmRule?.enabled ?? false}
              disabled={!alarmRule}
              type="checkbox"
              onChange={(event) =>
                updateAlarmRule(
                  alarmRule
                    ? { ...alarmRule, enabled: event.target.checked }
                    : alarmRule,
                )
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">码流</span>
          <span className="form-control">
            <select
              value={draft.stream}
              onChange={(event) =>
                updateDraft({
                  ...draft,
                  stream: event.target.value as StreamName,
                })
              }
            >
              <option value="sub">子码流</option>
              <option value="main">主码流</option>
            </select>
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
              value={draft.confidence_threshold}
              onChange={(event) =>
                updateDraft({
                  ...draft,
                  confidence_threshold: Number(event.target.value),
                })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">间隔 ms</span>
          <span className="form-control">
            <input
              min={1}
              step={100}
              type="number"
              value={draft.inference_interval_ms}
              onChange={(event) =>
                updateDraft({
                  ...draft,
                  inference_interval_ms: Number(event.target.value),
                })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">持续 ms</span>
          <span className="form-control">
            <input
              disabled={!alarmRule}
              min={0}
              step={100}
              type="number"
              value={alarmRule?.min_duration_ms ?? 0}
              onChange={(event) =>
                updateAlarmRule(
                  alarmRule
                    ? {
                        ...alarmRule,
                        min_duration_ms: Number(event.target.value),
                      }
                    : alarmRule,
                )
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
              value={draft.max_results}
              onChange={(event) =>
                updateDraft({ ...draft, max_results: Number(event.target.value) })
              }
            />
          </span>
        </label>

        <label className="form-field ai-model-path-field">
          <span className="form-label">模型</span>
          <span className="form-control">
            <input
              aria-invalid={modelMissing}
              value={draft.model_path}
              onChange={(event) =>
                updateDraft({ ...draft, model_path: event.target.value })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">输入宽</span>
          <span className="form-control">
            <input
              min={1}
              step={1}
              type="number"
              value={draft.input_width}
              onChange={(event) =>
                updateDraft({ ...draft, input_width: Number(event.target.value) })
              }
            />
          </span>
        </label>
        <label className="form-field">
          <span className="form-label">输入高</span>
          <span className="form-control">
            <input
              min={1}
              step={1}
              type="number"
              value={draft.input_height}
              onChange={(event) =>
                updateDraft({ ...draft, input_height: Number(event.target.value) })
              }
            />
          </span>
        </label>
      </div>

      <div className="ai-config-actions">
        <button
          type="button"
          disabled={saving}
          onClick={() => {
            if (status) {
              const rootConfig = {
                ...status.config,
                tasks: status.config.tasks.map((task) => ({ ...task })),
              };
              setRootDraft(rootConfig);
              setDraft({
                ...(rootConfig.tasks.find(
                  (task) => task.task === 'perimeter_detection',
                ) ?? rootConfig.tasks[0]),
              });
              setConfigDirty(false);
            }
            if (alarmConfig) {
              setAlarmRule({
                ...alarmConfig.ai_detection,
                regions: [...alarmConfig.ai_detection.regions],
              });
              setAlarmRuleDirty(false);
            }
            preserveSaveMessageOnStatusSync.current = false;
            setSaveMessage('');
          }}
        >
          恢复
        </button>
        <button
          type="button"
          className="primary"
          disabled={saving || modelMissing}
          onClick={saveEventConfig}
        >
          {saving ? '保存中' : '保存智能配置'}
        </button>
        {configDirty || alarmRuleDirty ? <span>有未保存修改</span> : null}
        {saveMessage ? <span>{saveMessage}</span> : null}
        {modelMissing ? <span className="form-error">模型路径不能为空</span> : null}
      </div>
    </section>
  );
}
