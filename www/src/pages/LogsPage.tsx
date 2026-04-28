export function LogsPage() {
  return (
    <section className="panel">
      <div className="page-heading">
        <div>
          <h2>日志信息</h2>
          <p>导出运行日志和用户操作审计记录。</p>
        </div>
      </div>
      <div className="log-actions">
        <a className="button-like" href="/api/logs/export">导出运行日志</a>
        <a className="button-like" href="/api/operations/export">导出操作审计</a>
      </div>
      <div className="log-table">
        <div><span>2026-04-26 10:20:12</span><strong>admin 登录系统</strong><em>Success</em></div>
        <div><span>2026-04-26 10:21:03</span><strong>修改视频配置</strong><em>Pending</em></div>
        <div><span>2026-04-26 10:22:18</span><strong>请求主码流抓图</strong><em>Success</em></div>
      </div>
    </section>
  );
}
