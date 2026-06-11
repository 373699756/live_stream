import { requestJson } from './client';
import type { OperationRecord } from './types';

export function getOperations(): Promise<{ items: OperationRecord[] }> {
    return requestJson<{ items: OperationRecord[] }>('/api/operations', {
        items: [],
    });
}

export function operationsExportUrl(): string {
    return '/api/operations/export';
}
