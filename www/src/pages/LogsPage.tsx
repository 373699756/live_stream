import { useEffect, useState } from 'react';
import { getOperations, operationsExportUrl } from '../api/operations';
import type { OperationRecord } from '../api/types';
import { formatTimestamp } from '../utils/format';

export function LogsPage() {
  const [records, setRecords] = useState<OperationRecord[]>([]);

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
        <a className="button-like" href={operationsExportUrl()}>导出操作审计</a>
      </div>
      <div className="log-table">
        {records.map((record) => (
          <div key={record.request_id || `${record.timestamp_ms}-${record.action}`}>
            <span>{formatTimestamp(record.timestamp_ms)}</span>
            <strong>{record.user_name || '-'} {record.action} {record.target}</strong>
            <em>{record.result}</em>
          </div>
        ))}
        {records.length === 0 && (
          <div><span>-</span><strong>暂无审计记录</strong><em>-</em></div>
        )}
      </div>
    </section>
  );
}
