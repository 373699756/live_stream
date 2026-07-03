import type { ReactNode } from 'react';

interface ConfigActionBarProps {
    canSave?: boolean;
    children?: ReactNode;
    error?: string;
    message?: string;
    onReset?: () => void;
    onSave: () => void;
    resetLabel?: string;
    saveLabel?: string;
    saving?: boolean;
}

export function ConfigActionBar({
    canSave = true,
    children,
    error,
    message,
    onReset,
    onSave,
    resetLabel = '恢复默认',
    saveLabel = '保存',
    saving = false,
}: ConfigActionBarProps) {
    return (
        <div className="config-action-area">
            <div className="config-action-status">
                {error ? (
                    <span className="action-status action-status-error">
                        {error}
                    </span>
                ) : message ? (
                    <span className="action-status action-status-info">
                        {message}
                    </span>
                ) : (
                    <span className="action-status action-status-muted">
                        修改后保存才会应用到设备
                    </span>
                )}
                {children}
            </div>
            <div className="config-action-buttons">
                {onReset ? (
                    <button type="button" onClick={onReset}>
                        {resetLabel}
                    </button>
                ) : null}
                <button
                    type="button"
                    className="primary"
                    disabled={!canSave || saving}
                    onClick={onSave}
                >
                    {saving ? '保存中' : saveLabel}
                </button>
            </div>
        </div>
    );
}
