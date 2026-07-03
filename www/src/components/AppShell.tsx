import type { ReactNode } from 'react';
import type { PageId } from '../pages/pageTypes';
import { navItems } from '../navigation';

interface AppShellProps {
    activePage: PageId;
    onNavigate: (page: PageId) => void;
    onLogout?: () => void;
    userName?: string;
    children: ReactNode;
}

export function AppShell({
    activePage,
    onNavigate,
    onLogout,
    userName,
    children,
}: AppShellProps) {
    const activeItem = navItems.find((item) => item.id === activePage);

    return (
        <div className="app-shell">
            <header className="topbar">
                <div className="brand-block">
                    <div className="brand-mark">IPC</div>
                    <div>
                        <div className="brand-title">IPC 管理台</div>
                        <div className="brand-subtitle">
                            {activeItem
                                ? `${activeItem.group} / ${activeItem.label}`
                                : '设备管理'}
                        </div>
                    </div>
                </div>
                <div className="topbar-status">
                    <span className="device-pill online">
                        <span className="status-dot online" />
                        设备在线
                    </span>
                    <span className="divider" />
                    <span className="user-pill">{userName || 'admin'}</span>
                    {onLogout && (
                        <>
                            <span className="divider" />
                            <button
                                type="button"
                                className="logout-btn"
                                onClick={onLogout}
                            >
                                退出
                            </button>
                        </>
                    )}
                </div>
            </header>

            <div className="workspace">
                <aside className="sidebar">
                    <div className="sidebar-title">设备功能</div>
                    {navItems.map((item, index) => {
                        const previous = navItems[index - 1];
                        const showGroup =
                            !previous || previous.group !== item.group;
                        return (
                            <div key={item.id}>
                                {showGroup && (
                                    <div className="nav-group">
                                        {item.group}
                                    </div>
                                )}
                                <button
                                    type="button"
                                    className={`nav-item ${activePage === item.id ? 'active' : ''}`}
                                    onClick={() => onNavigate(item.id)}
                                >
                                    <span className="nav-item-indicator" />
                                    {item.label}
                                </button>
                            </div>
                        );
                    })}
                </aside>
                <main className="content">{children}</main>
            </div>
        </div>
    );
}
