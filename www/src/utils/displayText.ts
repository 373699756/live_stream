export function formatTimestamp(timestampMs: number): string {
    if (timestampMs <= 0) {
        return '-';
    }
    return new Date(timestampMs).toLocaleString('zh-CN', {
        hour12: false,
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
    });
}

export function formatBytes(sizeBytes: number): string {
    if (sizeBytes <= 0) {
        return '-';
    }
    if (sizeBytes >= 1024 * 1024) {
        return `${(sizeBytes / (1024 * 1024)).toFixed(2)} MB`;
    }
    if (sizeBytes >= 1024) {
        return `${(sizeBytes / 1024).toFixed(1)} KB`;
    }
    return `${sizeBytes} B`;
}
