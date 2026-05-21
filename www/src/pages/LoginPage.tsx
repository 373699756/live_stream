import { useState } from 'react';

interface LoginPageProps {
  onLogin: (userName: string, password: string) => Promise<boolean>;
}

export function LoginPage({ onLogin }: LoginPageProps) {
  const [userName, setUserName] = useState('admin');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');

  const submit = async (event?: React.FormEvent<HTMLFormElement>) => {
    event?.preventDefault();
    setError('');
    const ok = await onLogin(userName, password);
    if (!ok) {
      setError('用户名或密码错误');
    }
  };

  return (
    <main className="login-page">
      <form className="login-panel" onSubmit={submit}>
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
        <button type="submit" className="primary wide">
          登录
        </button>
        {error && <div className="save-hint">{error}</div>}
      </form>
    </main>
  );
}
