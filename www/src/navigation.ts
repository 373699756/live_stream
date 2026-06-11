import type { PageId } from './pages/pageTypes';

export interface NavItem {
    id: PageId;
    label: string;
    group: string;
}

export const navItems: NavItem[] = [
    { id: 'live', label: '实时预览', group: 'Live' },
    { id: 'aiAlerts', label: 'AI 告警', group: 'Live' },
    { id: 'video', label: '视频参数', group: 'Setup' },
    { id: 'snapshot', label: '抓图参数', group: 'Setup' },
    { id: 'image', label: '图像参数', group: 'Setup' },
    { id: 'overlay', label: '视频叠加', group: 'Setup' },
    { id: 'network', label: '网络', group: 'Setup' },
    { id: 'system', label: '系统维护', group: 'System' },
    { id: 'streamInfo', label: '码流信息', group: 'Information' },
    { id: 'logs', label: '日志信息', group: 'Information' },
];
