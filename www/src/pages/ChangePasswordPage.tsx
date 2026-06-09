import { useState } from 'react';

interface ChangePasswordPageProps {
  onChangePassword: (oldPassword: string, newPassword: string) => Promise<boolean>;
  onLogout: () => void;
}

function passwordStrength(password: string): { label: string; className: string } {
  if (!password) {
    return { label: '未输入', className: 'password-meter-empty' };
  }
  const hasLetter = /[A-Za-z]/.test(password);
  const hasNumber = /\d/.test(password);
  const hasSymbol = /[^A-Za-z0-9]/.test(password);
  const score =
    Number(password.length >= 8) +
    Number(hasLetter) +
    Number(hasNumber) +
    Number(hasSymbol);
  if (score >= 4) {
    return { label: '较强', className: 'password-meter-strong' };
  }
  if (score >= 2) {
    return { label: '一般', className: 'password-meter-medium' };
  }
  return { label: '较弱', className: 'password-meter-weak' };
}

export function ChangePasswordPage({
  onChangePassword,
  onLogout,
}: ChangePasswordPageProps) {
  const [oldPassword, setOldPassword] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const strength = passwordStrength(newPassword);

  const submit = async (event?: React.FormEvent<HTMLFormElement>) => {
    event?.preventDefault();
    if (submitting) {
      return;
    }
    setError('');
    if (!oldPassword) {
      setError('请输入当前密码；出厂首次登录默认为 admin');
      return;
    }
    if (!newPassword) {
      setError('新密码不能为空');
      return;
    }
    if (newPassword === oldPassword) {
      setError('新密码不能和当前密码相同');
      return;
    }
    if (newPassword !== confirmPassword) {
      setError('两次输入的新密码不一致');
      return;
    }
    setSubmitting(true);
    try {
      const ok = await onChangePassword(oldPassword, newPassword);
      if (!ok) {
        setError('修改失败，请检查当前密码；连续错误会临时锁定账号');
      }
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <main className="login-page">
      <form className="login-panel change-password-panel" onSubmit={submit}>
        <div className="login-heading">
          <div className="login-brand">IPC</div>
          <div>
            <h1>设置管理密码</h1>
            <p>首次登录后需要修改出厂密码</p>
          </div>
        </div>
        <div className="auth-field-list">
          <label className="auth-field">
            <span>当前密码</span>
            <input
              type="password"
              autoFocus
              autoComplete="current-password"
              value={oldPassword}
              onChange={(event) => setOldPassword(event.target.value)}
            />
          </label>
          <label className="auth-field">
            <span>新密码</span>
            <input
              type="password"
              autoComplete="new-password"
              value={newPassword}
              onChange={(event) => setNewPassword(event.target.value)}
            />
          </label>
          <div className={`password-meter ${strength.className}`}>
            <span>强度</span>
            <strong>{strength.label}</strong>
          </div>
          <label className="auth-field">
            <span>确认新密码</span>
            <input
              type="password"
              autoComplete="new-password"
              value={confirmPassword}
              onChange={(event) => setConfirmPassword(event.target.value)}
            />
          </label>
        </div>
        <div className="auth-action-row">
          <button type="submit" className="primary" disabled={submitting}>
            {submitting ? '保存中...' : '保存密码'}
          </button>
          <button type="button" className="secondary-action" onClick={onLogout}>
            退出登录
          </button>
        </div>
        {error && <div className="auth-message auth-message-error">{error}</div>}
      </form>
    </main>
  );
}
