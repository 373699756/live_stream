// System status API

import { mockSystemStatus } from './mockSystem';
import { requestJson, type ApiRequestOptions } from './client';
import type { SystemStatus } from './types';

export function getSystemStatus(
    init?: ApiRequestOptions,
): Promise<SystemStatus> {
    return requestJson<SystemStatus>(
        '/api/system/status',
        mockSystemStatus,
        init,
    );
}
