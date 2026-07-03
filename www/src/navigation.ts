import type { PageId } from './pages/pageTypes';

export interface NavItem {
    id: PageId;
    label: string;
    group: string;
}

export const navItems: NavItem[] = [
    { id: 'live', label: '实时预览', group: '预览' },
    { id: 'aiAlerts', label: 'AI 告警', group: '预览' },
    { id: 'video', label: '视频参数', group: '配置' },
    { id: 'snapshot', label: '抓图参数', group: '配置' },
    { id: 'image', label: '图像参数', group: '配置' },
    { id: 'overlay', label: '视频叠加', group: '配置' },
    { id: 'network', label: '网络', group: '配置' },
    { id: 'system', label: '系统维护', group: '系统' },
    { id: 'streamInfo', label: '码流信息', group: '信息' },
    { id: 'logs', label: '日志信息', group: '信息' },
];
