// System info API

import { mockSystemInfo } from './mockSystem';
import { requestJson, type ApiRequestOptions } from './client';
import type { SystemInfo } from './types/system';

export function getSystemInfo(
    init?: ApiRequestOptions,
): Promise<SystemInfo> {
    return requestJson<SystemInfo>(
        '/api/system/status',
        mockSystemInfo,
        init,
    );
}
