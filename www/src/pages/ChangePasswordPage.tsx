import { useState } from 'react';

interface ChangePasswordPageProps {
  onChangePassword: (oldPassword: string, newPassword: string) => Promise<boolean>;
  onLogout: () => void;
}

export function ChangePasswordPage({
  onChangePassword,
  onLogout,
}: ChangePasswordPageProps) {
  const [oldPassword, setOldPassword] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [error, setError] = useState('');

  const submit = async (event?: React.FormEvent<HTMLFormElement>) => {
    event?.preventDefault();
    setError('');
    if (!newPassword) {
      setError('新密码不能为空');
      return;
    }
    if (newPassword !== confirmPassword) {
      setError('两次输入的新密码不一致');
      return;
    }
    const ok = await onChangePassword(oldPassword, newPassword);
    if (!ok) {
      setError('旧密码错误或新密码不可用');
    }
  };

  return (
    <main className="login-page">
      <form className="login-panel" onSubmit={submit}>
        <div className="login-brand">IPC</div>
        <h1>首次登录修改密码</h1>
        <p>请设置设备管理密码</p>
        <label>
          旧密码
          <input
            type="password"
            autoComplete="current-password"
            value={oldPassword}
            onChange={(event) => setOldPassword(event.target.value)}
          />
        </label>
        <label>
          新密码
          <input
            type="password"
            autoComplete="new-password"
            value={newPassword}
            onChange={(event) => setNewPassword(event.target.value)}
          />
        </label>
        <label>
          确认新密码
          <input
            type="password"
            autoComplete="new-password"
            value={confirmPassword}
            onChange={(event) => setConfirmPassword(event.target.value)}
          />
        </label>
        <button type="submit" className="primary wide">
          保存密码
        </button>
        <button type="button" className="wide secondary-action" onClick={onLogout}>
          退出登录
        </button>
        {error && <div className="save-hint">{error}</div>}
      </form>
    </main>
  );
}
