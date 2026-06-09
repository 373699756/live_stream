import { useEffect, useMemo, useRef, useState } from 'react';
import type { AiTaskName } from '../api/types';
import { AiAlertGrid } from '../features/ai-alerts/AiAlertGrid';
import { AiCommonConfigPanel } from '../features/ai-alerts/AiCommonConfigPanel';
import { AiEventTaskPanel } from '../features/ai-alerts/AiEventTaskPanel';
import { AiRuntimeSummary } from '../features/ai-alerts/AiRuntimeSummary';
import {
  alertsForTask,
  alertGroupsByTask,
  emptyTextForTask,
  kAiEventTabs,
  tabStateLabel,
  taskLabel,
} from '../features/ai-alerts/aiAlertTasks';
import {
  latestAlarmTimeText,
  latestTimeText,
} from '../features/ai-alerts/aiAlertFormat';
import { AiPerimeterEditor } from '../features/ai-alerts/AiPerimeterEditor';
import { useAiAlerts } from '../hooks/useAiAlerts';
import '../styles/ai-alerts.css';

export function AiAlertsPage() {
  const {
    status,
    alarmConfig,
    alarmStatus,
    lastAlarmEvent,
    alerts,
    loading,
    error,
    refresh,
  } = useAiAlerts();
  const [activeTask, setActiveTask] =
    useState<AiTaskName>('perimeter_detection');
  const initialTaskSynced = useRef(false);

  useEffect(() => {
    if (!status || initialTaskSynced.current) {
      return;
    }
    setActiveTask(status.config.task);
    initialTaskSynced.current = true;
  }, [status]);

  const sortedAlerts = useMemo(
    () => [...alerts].sort((left, right) => right.timestamp_ms - left.timestamp_ms),
    [alerts],
  );
  const alertGroups = useMemo(
    () => alertGroupsByTask(sortedAlerts),
    [sortedAlerts],
  );
  const activeTab =
    kAiEventTabs.find((tab) => tab.task === activeTask) ?? kAiEventTabs[0];
  const activeAlerts = alertsForTask(alertGroups, activeTask);

  return (
    <div className="page-grid ai-page-grid">
      <div className="page-heading">
        <div>
          <h2>AI 告警</h2>
          <p>按任务查看最近抓拍；设备当前只运行一个 AI 任务</p>
        </div>
        <button
          type="button"
          className="primary"
          disabled={loading}
          onClick={() => {
            void refresh();
          }}
        >
          刷新
        </button>
      </div>

      <AiRuntimeSummary
        status={status}
        alarmConfig={alarmConfig}
        alarmStatus={alarmStatus}
        lastAlarmEvent={lastAlarmEvent}
      />

      {error ? <div className="status-note error-note">{error}</div> : null}

      <AiCommonConfigPanel
        status={status}
        alarmConfig={alarmConfig}
        onSaved={refresh}
      />

      <section className="panel wide-panel ai-events-panel">
        <div className="ai-event-tabs" role="tablist" aria-label="AI 抓拍分类">
          {kAiEventTabs.map((tab) => {
            const selected = tab.task === activeTask;
            return (
              <button
                type="button"
                role="tab"
                aria-controls={`ai-event-${tab.task}`}
                aria-selected={selected}
                className={selected ? 'ai-event-tab active' : 'ai-event-tab'}
                key={tab.task}
                onClick={() => setActiveTask(tab.task)}
              >
                <span className="ai-event-tab-main">
                  <strong>{tab.label}</strong>
                  <em>{tabStateLabel(status, tab.task)}</em>
                </span>
                <span className="ai-event-tab-count">
                  {loading ? '...' : alertsForTask(alertGroups, tab.task).length}
                </span>
              </button>
            );
          })}
        </div>

        <div
          id={`ai-event-${activeTask}`}
          className="ai-event-page"
          role="tabpanel"
        >
          <div className="ai-event-toolbar">
            <div>
              <h2>{activeTab.title}</h2>
              <p>{taskLabel(activeTask)}</p>
            </div>
            <div className="ai-event-stats">
              <span>
                运行{' '}
                <strong>
                  {status?.config.task === activeTask ? '当前任务' : '未运行'}
                </strong>
              </span>
              <span>
                抓拍 <strong>{activeAlerts.length}</strong>
              </span>
              <span>
                上限 <strong>10 张</strong>
              </span>
              <span>
                最近抓拍 <strong>{latestTimeText(activeAlerts)}</strong>
              </span>
              <span>
                系统报警{' '}
                <strong>{latestAlarmTimeText(alarmStatus, lastAlarmEvent)}</strong>
              </span>
            </div>
          </div>

          <AiEventTaskPanel
            status={status}
            activeTask={activeTask}
            onSaved={refresh}
          />

          {activeTask === 'perimeter_detection' ? (
            <AiPerimeterEditor status={status} onSaved={refresh} />
          ) : null}

          {activeAlerts.length === 0 ? (
            <div className="ai-event-empty">
              <strong>{loading ? '正在加载' : activeTab.emptyTitle}</strong>
              <span>
                {loading
                  ? '读取 AI 抓拍记录...'
                  : emptyTextForTask(status, activeTab, activeTask)}
              </span>
            </div>
          ) : (
            <AiAlertGrid alerts={activeAlerts} />
          )}
        </div>
      </section>
    </div>
  );
}
