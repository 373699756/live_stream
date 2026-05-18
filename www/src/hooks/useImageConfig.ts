/**
 * useImageConfig — fetch image config together with capabilities and stream
 * status (needed for the live preview panel in ImageConfigPage).
 */

import { useEffect, useState } from 'react';
import { getImageConfig, saveImageConfig } from '../api/image';
import { getMediaCapabilities, getStreamStatus } from '../api/video';
import type { ImageConfig, MediaCapabilities, StreamStatus } from '../api/types';
import { mockMediaCapabilities, mockImageConfig } from '../api/mock';
import { cloneDefaultConfig } from '../api/mock';

export function useImageConfig() {
  const [config, setConfig] = useState<ImageConfig | null>(null);
  const [capabilities, setCapabilities] = useState<MediaCapabilities>(mockMediaCapabilities);
  const [statuses, setStatuses] = useState<StreamStatus[]>([]);
  const [savedMsg, setSavedMsg] = useState('');

  useEffect(() => {
    void getImageConfig().then((c) => { if (c !== null) setConfig(c); });
    void getMediaCapabilities().then(setCapabilities);
    void getStreamStatus().then(setStatuses);
  }, []);

  const save = async () => {
    if (config === null) return;
    const ok = await saveImageConfig(config);
    setSavedMsg(ok ? '已提交保存' : '后端未连接，已保留本地修改');
  };

  const reset = () => {
    setConfig(cloneDefaultConfig(mockImageConfig));
    setSavedMsg('已恢复默认值，保存后生效');
  };

  return { config, setConfig, capabilities, statuses, save, reset, savedMsg };
}
