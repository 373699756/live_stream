import { lazy, Suspense, useState } from 'react';
import { AppShell } from './components/AppShell';
import { useAuth } from './context/AuthContext';
import { ChangePasswordPage } from './pages/ChangePasswordPage';
import { LiveViewPage } from './pages/LiveViewPage';
import { LoginPage } from './pages/LoginPage';
import type { PageId } from './pages/pageTypes';

const AiAlertsPage = lazy(() =>
  import('./pages/AiAlertsPage').then((module) => ({
    default: module.AiAlertsPage,
  })),
);
const ImageConfigPage = lazy(() =>
  import('./pages/ImageConfigPage').then((module) => ({
    default: module.ImageConfigPage,
  })),
);
const LogsPage = lazy(() =>
  import('./pages/LogsPage').then((module) => ({
    default: module.LogsPage,
  })),
);
const NetworkConfigPage = lazy(() =>
  import('./pages/NetworkConfigPage').then((module) => ({
    default: module.NetworkConfigPage,
  })),
);
const OverlayConfigPage = lazy(() =>
  import('./pages/OverlayConfigPage').then((module) => ({
    default: module.OverlayConfigPage,
  })),
);
const SnapshotConfigPage = lazy(() =>
  import('./pages/SnapshotConfigPage').then((module) => ({
    default: module.SnapshotConfigPage,
  })),
);
const StreamInfoPage = lazy(() =>
  import('./pages/StreamInfoPage').then((module) => ({
    default: module.StreamInfoPage,
  })),
);
const SystemPage = lazy(() =>
  import('./pages/SystemPage').then((module) => ({
    default: module.SystemPage,
  })),
);
const VideoConfigPage = lazy(() =>
  import('./pages/VideoConfigPage').then((module) => ({
    default: module.VideoConfigPage,
  })),
);

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
      <Suspense fallback={<div className="panel">正在加载页面...</div>}>
        {renderPage(page)}
      </Suspense>
    </AppShell>
  );
}
