export interface MediaEvent {
    type: string;
    source: string;
    target: string;
    msg: string;
    value: number;
    timestamp_ms: number;
    level: number;
}

export function openMediaEvents(
    onEvent: (event: MediaEvent) => void,
): EventSource {
    const source = new EventSource('/api/events');
    const handleMsg = (msg: MessageEvent<string>) => {
        try {
            onEvent(JSON.parse(msg.data) as MediaEvent);
        } catch {
            // Ignore malformed event frames; periodic status refresh remains active.
        }
    };

    source.addEventListener('media_status_changed', handleMsg);
    source.addEventListener('stream_started', handleMsg);
    source.addEventListener('stream_stopped', handleMsg);
    source.addEventListener('net_queue_changed', handleMsg);
    source.addEventListener('media_subscription_changed', handleMsg);
    source.addEventListener('alarm_on', handleMsg);
    source.addEventListener('alarm_off', handleMsg);
    source.onmessage = handleMsg;
    return source;
}
