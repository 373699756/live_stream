interface LoginPageProps {
  onLogin: () => void;
}

export function LoginPage({ onLogin }: LoginPageProps) {
  return (
    <main className="login-page">
      <section className="login-panel">
        <div className="login-brand">IPC</div>
        <h1>Live Stream IPC</h1>
        <p>设备 Web 管理控制台</p>
        <label>
          用户名
          <input defaultValue="admin" />
        </label>
        <label>
          密码
          <input type="password" autoComplete="current-password" />
        </label>
        <button type="button" className="primary wide" onClick={onLogin}>
          登录
        </button>
      </section>
    </main>
  );
}
