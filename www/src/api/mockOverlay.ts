import type { OverlayConfig } from './types/media/configuration';

const emptyMainMask = () => ({
    enabled: false,
    x: 0,
    y: 0,
    width: 160,
    height: 120,
    color: '#000000',
});

const emptySubMask = () => ({
    enabled: false,
    x: 0,
    y: 0,
    width: 80,
    height: 60,
    color: '#000000',
});

export const mockOverlayConfig: OverlayConfig = {
    enabled: true,
    items: {
        timestamp: { enabled: true, format: '%Y-%m-%d %H:%M:%S', x: 16, y: 16 },
        device_name: { enabled: true, text: 'IPC Camera', x: 16, y: 48 },
    },
    font_size: 24,
    font_color: '#FFFFFF',
    background: true,
    privacy_masks: {
        main: [
            emptyMainMask(),
            emptyMainMask(),
            emptyMainMask(),
            emptyMainMask(),
        ],
        sub: [emptySubMask(), emptySubMask(), emptySubMask(), emptySubMask()],
    },
};
