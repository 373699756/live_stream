import { requestJson } from './client';
import type { OperationRecord } from './types/operation';

export function getOperations(): Promise<{ items: OperationRecord[] }> {
    return requestJson<{ items: OperationRecord[] }>('/api/operations', {
        items: [],
    });
}

export function operationsExportUrl(): string {
    return '/api/operations/export';
}
