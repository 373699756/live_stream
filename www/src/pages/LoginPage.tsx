import { useState } from 'react';
import { login } from '../api/client';

interface LoginPageProps {
  onLogin: () => void;
}

export function LoginPage({ onLogin }: LoginPageProps) {
  const [userName, setUserName] = useState('admin');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');

  const submit = async () => {
    setError('');
    const ok = await login(userName, password);
    if (ok) {
      onLogin();
      return;
    }
    setError('用户名或密码错误');
  };

  return (
    <main className="login-page">
      <section className="login-panel">
        <div className="login-brand">IPC</div>
        <h1>Live Stream IPC</h1>
        <p>设备 Web 管理控制台</p>
        <label>
          用户名
          <input value={userName} onChange={(event) => setUserName(event.target.value)} />
        </label>
        <label>
          密码
          <input
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
          />
        </label>
        <button type="button" className="primary wide" onClick={submit}>
          登录
        </button>
        {error && <div className="save-hint">{error}</div>}
      </section>
    </main>
  );
}
