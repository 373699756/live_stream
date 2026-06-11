import { Component, lazy, Suspense, useState, type ReactNode } from 'react';
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

interface PageErrorBoundaryProps {
    children: ReactNode;
    page: PageId;
}

interface PageErrorBoundaryState {
    message: string;
}

class PageErrorBoundary extends Component<
    PageErrorBoundaryProps,
    PageErrorBoundaryState
> {
    state: PageErrorBoundaryState = { message: '' };

    static getDerivedStateFromError(error: unknown): PageErrorBoundaryState {
        return {
            message: error instanceof Error ? error.message : '页面加载失败',
        };
    }

    componentDidUpdate(previousProps: PageErrorBoundaryProps) {
        if (previousProps.page !== this.props.page && this.state.message) {
            this.setState({ message: '' });
        }
    }

    render() {
        if (this.state.message) {
            return (
                <div className="panel">
                    <div className="status-note error-note">
                        页面加载失败：{this.state.message}
                    </div>
                </div>
            );
        }
        return this.props.children;
    }
}

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
        principal,
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
        <AppShell
            activePage={page}
            onNavigate={setPage}
            onLogout={logout}
            userName={principal?.user_name}
        >
            <PageErrorBoundary page={page}>
                <Suspense
                    fallback={<div className="panel">正在加载页面...</div>}
                >
                    {renderPage(page)}
                </Suspense>
            </PageErrorBoundary>
        </AppShell>
    );
}
