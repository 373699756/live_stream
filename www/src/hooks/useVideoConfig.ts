/**
 * useVideoConfig — fetch video config, media capabilities, and stream status.
 *
 * Wraps the three parallel fetches that VideoConfigPage and ImageConfigPage
 * both need. Returns loading state so pages can show a spinner/placeholder.
 */

import { useEffect, useState } from 'react';
import { getVideoConfig, getMediaCapabilities, getStreamStatus } from '../api/video';
import type { VideoConfig, MediaCapabilities, StreamStatus } from '../api/types';
import { mockMediaCapabilities } from '../api/mock';

export function useVideoConfig() {
  const [config, setConfig] = useState<VideoConfig | null>(null);
  const [capabilities, setCapabilities] = useState<MediaCapabilities>(mockMediaCapabilities);
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);

  useEffect(() => {
    void getVideoConfig().then((c) => { if (c !== null) setConfig(c); });
    void getMediaCapabilities().then(setCapabilities);
    void getStreamStatus().then(setStatuses);
  }, []);

  return { config, setConfig, capabilities, statuses };
}
