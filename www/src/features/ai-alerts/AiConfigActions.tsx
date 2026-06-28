import type { AiConfig } from '../../api/types';

interface AiConfigActionsProps {
    alarmDirty: boolean;
    dirty: boolean;
    draft: AiConfig | null;
    restoreDraft: () => void;
    saveDraft: () => void;
    saveMsg: string;
    saving: boolean;
}

export function AiConfigActions({
    alarmDirty,
    dirty,
    draft,
    restoreDraft,
    saveDraft,
    saveMsg,
    saving,
}: AiConfigActionsProps) {
    return (
        <div className="ai-config-actions">
            {dirty || alarmDirty ? <span>有未保存修改</span> : null}
            {saveMsg ? <span>{saveMsg}</span> : null}
            <button type="button" disabled={!draft || saving} onClick={restoreDraft}>
                恢复
            </button>
            <button
                type="button"
                className="primary"
                disabled={!draft || saving}
                onClick={saveDraft}
            >
                {saving ? '保存中' : '保存配置'}
            </button>
        </div>
    );
}
