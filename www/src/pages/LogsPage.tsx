import { useEffect, useMemo, useState } from 'react';
import { getOperations, operationsExportUrl } from '../api/operations';
import type {
    OperationAction,
    OperationRecord,
    OperationResult,
} from '../api/types';
import { formatTimestamp } from '../utils/format';

const actionLabels: Record<OperationAction, string> = {
    Login: '登录',
    Logout: '退出登录',
    AuthFailed: '认证失败',
    TokenExpired: '会话过期',
    ModifyConfig: '修改配置',
    Reboot: '重启设备',
    FactoryReset: '恢复出厂',
    Upgrade: '升级',
    TimeSync: '时间同步',
    NetworkChange: '网络变更',
    UserManage: '用户管理',
    PermissionDenied: '权限拒绝',
};

const resultLabels: Record<OperationResult, string> = {
    Success: '成功',
    Failed: '失败',
    Rejected: '已拒绝',
};

const resultClasses: Record<OperationResult, string> = {
    Success: 'success',
    Failed: 'failed',
    Rejected: 'rejected',
};

function actionText(action: OperationAction) {
    return actionLabels[action];
}

function resultText(result: OperationResult) {
    return resultLabels[result];
}

function resultClass(result: OperationResult) {
    return resultClasses[result];
}

function operationTitle(record: OperationRecord) {
    const target = record.target || record.module;
    return target
        ? `${actionText(record.action)}：${target}`
        : actionText(record.action);
}

function operationMeta(record: OperationRecord) {
    return [
        record.user_name || '未知用户',
        record.module || '未知模块',
        record.client_ip || '',
    ]
        .filter(Boolean)
        .join(' / ');
}

export function LogsPage() {
    const [records, setRecords] = useState<OperationRecord[]>([]);
    const sortedRecords = useMemo(
        () =>
            [...records].sort(
                (left, right) => right.timestamp_ms - left.timestamp_ms,
            ),
        [records],
    );

    useEffect(() => {
        void getOperations().then((body) => setRecords(body.items));
    }, []);

    return (
        <section className="panel">
            <div className="page-heading">
                <div>
                    <h2>日志信息</h2>
                    <p>查看和导出用户操作审计记录。</p>
                </div>
            </div>
            <div className="log-actions">
                <a className="button-like" href={operationsExportUrl()}>
                    导出操作审计
                </a>
            </div>
            <div className="log-table">
                <div className="log-row log-header">
                    <span>操作时间</span>
                    <strong>操作内容</strong>
                    <em>操作状态</em>
                </div>
                {sortedRecords.map((record) => (
                    <div
                        className="log-row"
                        key={
                            record.request_id ||
                            `${record.timestamp_ms}-${record.action}`
                        }
                    >
                        <span className="log-time">
                            {formatTimestamp(record.timestamp_ms)}
                        </span>
                        <strong className="log-content">
                            <span className="log-action">
                                {operationTitle(record)}
                            </span>
                            <small>{operationMeta(record)}</small>
                        </strong>
                        <em
                            className={`log-result ${resultClass(record.result)}`}
                        >
                            <span>{resultText(record.result)}</span>
                            {record.reason ? (
                                <small>{record.reason}</small>
                            ) : null}
                        </em>
                    </div>
                ))}
                {sortedRecords.length === 0 && (
                    <div className="log-row log-empty">
                        <span>-</span>
                        <strong>暂无审计记录</strong>
                        <em>-</em>
                    </div>
                )}
            </div>
        </section>
    );
}
