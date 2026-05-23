import type { ReactNode } from 'react';
import type { PageId } from '../pages/pageTypes';

interface NavItem {
  id: PageId;
  label: string;
  group: string;
}

const navItems: NavItem[] = [
  { id: 'live', label: '实时预览', group: 'Live' },
  { id: 'video', label: '视频参数', group: 'Setup' },
  { id: 'snapshot', label: '抓图参数', group: 'Setup' },
  { id: 'image', label: '图像参数', group: 'Setup' },
  { id: 'overlay', label: '视频叠加', group: 'Setup' },
  { id: 'network', label: '网络', group: 'Setup' },
  { id: 'system', label: '系统维护', group: 'System' },
  { id: 'streamInfo', label: '码流信息', group: 'Information' },
  { id: 'logs', label: '日志信息', group: 'Information' },
];

interface AppShellProps {
  activePage: PageId;
  onNavigate: (page: PageId) => void;
  onLogout?: () => void;
  children: ReactNode;
}

export function AppShell({ activePage, onNavigate, onLogout, children }: AppShellProps) {
  return (
    <div className="app-shell">
      <header className="topbar">
        <div className="brand-block">
          <div className="brand-mark">IPC</div>
          <div>
            <div className="brand-title">Live Stream IPC</div>
            <div className="brand-subtitle">Web Management Console</div>
          </div>
        </div>
        <div className="topbar-status">
          <span className="status-dot online" />
          设备在线
          <span className="divider" />
          admin
          {onLogout && (
            <>
              <span className="divider" />
              <button type="button" className="logout-btn" onClick={onLogout}>退出</button>
            </>
          )}
        </div>
      </header>

      <div className="workspace">
        <aside className="sidebar">
          <div className="sidebar-title">功能菜单</div>
          {navItems.map((item, index) => {
            const previous = navItems[index - 1];
            const showGroup = !previous || previous.group !== item.group;
            return (
              <div key={item.id}>
                {showGroup && <div className="nav-group">{item.group}</div>}
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
