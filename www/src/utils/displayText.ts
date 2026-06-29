export function formatTimestamp(timestampMs: number): string {
    if (timestampMs <= 0) {
        return '-';
    }
    return new Date(timestampMs).toLocaleString();
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
