export interface MediaEvent {
    type: string;
    source: string;
    target: string;
    message: string;
    value: number;
    timestamp_ms: number;
    level: number;
}

export function openMediaEvents(
    onEvent: (event: MediaEvent) => void,
): EventSource {
    const source = new EventSource('/api/events');
    const handleMessage = (message: MessageEvent<string>) => {
        try {
            onEvent(JSON.parse(message.data) as MediaEvent);
        } catch {
            // Ignore malformed event frames; periodic status refresh remains active.
        }
    };

    source.addEventListener('media_status_changed', handleMessage);
    source.addEventListener('alarm_on', handleMessage);
    source.addEventListener('alarm_off', handleMessage);
    source.onmessage = handleMessage;
    return source;
}
