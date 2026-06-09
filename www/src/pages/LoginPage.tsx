import { useState } from 'react';

interface LoginPageProps {
  onLogin: (userName: string, password: string) => Promise<{ ok: boolean; error?: string }>;
}

function loginErrorMessage(error?: string): string {
  if (error === 'network_error') {
    return '设备无响应，请检查网络后重试';
  }
  return '用户名或密码错误；连续错误会临时锁定账号';
}

export function LoginPage({ onLogin }: LoginPageProps) {
  const [userName, setUserName] = useState('admin');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);

  const submit = async (event?: React.FormEvent<HTMLFormElement>) => {
    event?.preventDefault();
    if (submitting) {
      return;
    }
    setSubmitting(true);
    setError('');
    try {
      const result = await onLogin(userName.trim(), password);
      if (!result.ok) {
        setError(loginErrorMessage(result.error));
      }
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <main className="login-page">
      <form className="login-panel" onSubmit={submit}>
        <div className="login-heading">
          <div className="login-brand">IPC</div>
          <div>
            <h1>Live Stream IPC</h1>
            <p>设备 Web 管理控制台</p>
          </div>
        </div>
        <div className="auth-field-list">
          <label className="auth-field">
            <span>用户名</span>
            <input
              autoComplete="username"
              value={userName}
              onChange={(event) => setUserName(event.target.value)}
            />
          </label>
          <label className="auth-field">
            <span>密码</span>
            <input
              type="password"
              autoFocus
              autoComplete="current-password"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
            />
          </label>
        </div>
        <button type="submit" className="primary wide" disabled={submitting}>
          {submitting ? '登录中...' : '登录'}
        </button>
        {error && <div className="auth-message auth-message-error">{error}</div>}
      </form>
    </main>
  );
}
