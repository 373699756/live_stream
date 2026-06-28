export type OperationAction =
    | 'Login'
    | 'Logout'
    | 'AuthFailed'
    | 'TokenExpired'
    | 'ModifyConfig'
    | 'Reboot'
    | 'FactoryReset'
    | 'Upgrade'
    | 'TimeSync'
    | 'NetworkChange'
    | 'UserManage'
    | 'PermissionDenied';

export type OperationResult = 'Success' | 'Failed' | 'Rejected';

export interface OperationRecord {
    timestamp_ms: number;
    request_id: string;
    user_name: string;
    session_id: string;
    client_ip: string;
    module: string;
    action: OperationAction;
    target: string;
    result: OperationResult;
    reason: string;
}
