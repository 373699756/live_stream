import { useState } from 'react';
import { AppShell } from './components/AppShell';
import { useAuth } from './context/AuthContext';
import { AiAlertsPage } from './pages/AiAlertsPage';
import { ImageConfigPage } from './pages/ImageConfigPage';
import { LiveViewPage } from './pages/LiveViewPage';
import { ChangePasswordPage } from './pages/ChangePasswordPage';
import { LoginPage } from './pages/LoginPage';
import { LogsPage } from './pages/LogsPage';
import { NetworkConfigPage } from './pages/NetworkConfigPage';
import { OverlayConfigPage } from './pages/OverlayConfigPage';
import type { PageId } from './pages/pageTypes';
import { SnapshotConfigPage } from './pages/SnapshotConfigPage';
import { StreamInfoPage } from './pages/StreamInfoPage';
import { SystemPage } from './pages/SystemPage';
import { VideoConfigPage } from './pages/VideoConfigPage';

function renderPage(page: PageId) {
  switch (page) {
    case 'live':
      return <LiveViewPage />;
    case 'aiAlerts':
      return <AiAlertsPage />;
    case 'streamInfo':
      return <StreamInfoPage />;
    case 'video':
      return <VideoConfigPage />;
    case 'snapshot':
      return <SnapshotConfigPage />;
    case 'image':
      return <ImageConfigPage />;
    case 'overlay':
      return <OverlayConfigPage />;
    case 'network':
      return <NetworkConfigPage />;
    case 'system':
      return <SystemPage />;
    case 'logs':
      return <LogsPage />;
  }
}

export default function App() {
  const {
    authenticated,
    mustChangePassword,
    ready,
    login,
    changePassword,
    logout,
  } = useAuth();
  const [page, setPage] = useState<PageId>('live');

  if (!ready) {
    return (
      <main className="login-page">
        <div className="login-panel">
          <div className="login-brand">IPC</div>
          <h1>Live Stream IPC</h1>
          <p>正在校验登录状态...</p>
        </div>
      </main>
    );
  }

  if (!authenticated) {
    return <LoginPage onLogin={login} />;
  }

  if (mustChangePassword) {
    return (
      <ChangePasswordPage
        onChangePassword={changePassword}
        onLogout={logout}
      />
    );
  }

  return (
    <AppShell activePage={page} onNavigate={setPage} onLogout={logout}>
      {renderPage(page)}
    </AppShell>
  );
}
