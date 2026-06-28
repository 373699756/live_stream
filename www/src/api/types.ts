export * from './types/core';
export * from './types/ai';
export * from './types/media';
export * from './types/upgrade';
export * from './types/alarm';
export * from './types/time';
export * from './types/operation';

export interface AuthPrincipal {
    user_name: string;
    session_id: string;
    role: string;
    must_change_password?: boolean;
}

export interface AuthState {
    authenticated: boolean;
    mustChangePassword: boolean;
    principal?: AuthPrincipal;
}

export interface SystemInfo {
    deviceName: string;
    model: string;
    firmware: string;
    uptime: string;
    cpu: number;
    memory: number;
    temperature: number;
    modules: Array<{ name: string; state: 'running' | 'pending' | 'error' }>;
}
