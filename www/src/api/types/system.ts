export interface SystemInfo {
    deviceName: string;
    model: string;
    firmware: string;
    software: string;
    uptime: string;
    cpu: number;
    memory: number;
    temperature: number;
    modules: Array<{ name: string; state: 'running' | 'pending' | 'error' }>;
}
