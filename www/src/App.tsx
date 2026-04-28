import { useState } from 'react';
import { AppShell } from './components/AppShell';
import { hasToken } from './api/client';
import { ImageConfigPage } from './pages/ImageConfigPage';
import { LiveViewPage } from './pages/LiveViewPage';
import { LoginPage } from './pages/LoginPage';
import { LogsPage } from './pages/LogsPage';
import { NetworkConfigPage } from './pages/NetworkConfigPage';
import { OsdConfigPage } from './pages/OsdConfigPage';
import type { PageId } from './pages/pageTypes';
import { SnapshotConfigPage } from './pages/SnapshotConfigPage';
import { SystemPage } from './pages/SystemPage';
import { VideoConfigPage } from './pages/VideoConfigPage';

function renderPage(page: PageId) {
  switch (page) {
    case 'live':
      return <LiveViewPage />;
    case 'video':
      return <VideoConfigPage />;
    case 'snapshot':
      return <SnapshotConfigPage />;
    case 'image':
      return <ImageConfigPage />;
    case 'osd':
      return <OsdConfigPage />;
    case 'network':
      return <NetworkConfigPage />;
    case 'system':
      return <SystemPage />;
    case 'logs':
      return <LogsPage />;
  }
}

export default function App() {
  const [authenticated, setAuthenticated] = useState(hasToken());
  const [page, setPage] = useState<PageId>('live');

  if (!authenticated) {
    return <LoginPage onLogin={() => setAuthenticated(true)} />;
  }

  return (
    <AppShell activePage={page} onNavigate={setPage}>
      {renderPage(page)}
    </AppShell>
  );
}
