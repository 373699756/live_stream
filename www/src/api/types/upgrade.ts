export type UpgradeState =
    | 'idle'
    | 'validating'
    | 'preparing'
    | 'writing'
    | 'committing'
    | 'waiting_reboot'
    | 'completed'
    | 'failed'
    | 'canceled';

export interface UpgradePackageInfo {
    package_path: string;
    version: string;
    size_bytes: number;
    digest: string;
    build_time_ms: number;
    target_model: string;
    requires_reboot: boolean;
}

export interface UpgradeInfo {
    state: UpgradeState;
    progress_percent: number;
    current_stage: string;
    target_version: string;
    ok: boolean;
    error_message: string;
    started_at_ms: number;
    finished_at_ms: number;
}

export interface UpgradeRequest {
    package_path: string;
    expected_version: string;
    allow_same_version: boolean;
    allow_downgrade: boolean;
    auto_reboot: boolean;
}
