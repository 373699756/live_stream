/**
 * useConfigForm — generic hook for a load-edit-save config page.
 *
 * Handles the common pattern shared by every config page:
 *   1. Fetch config on mount via `fetchFn`.
 *   2. Expose `config` and a setter that keeps local draft state.
 *   3. `save()` calls `saveFn` and updates `savedMsg` with the result.
 *   4. `reset()` replaces draft with `defaultValue` (a deep clone).
 *
 * Usage:
 *   const { config, setConfig, save, reset, savedMsg } =
 *     useConfigForm(getVideoConfig, saveVideoConfig, mockVideoConfig);
 */

import { useEffect, useState } from 'react';
import { cloneDefaultConfig } from '../api/mock';

export function useConfigForm<T>(
  fetchFn: () => Promise<T | null>,
  saveFn: (config: T) => Promise<boolean>,
  defaultValue: T,
) {
  const [config, setConfig] = useState<T | null>(null);
  const [savedMsg, setSavedMsg] = useState('');

  useEffect(() => {
    void fetchFn().then((result) => {
      if (result !== null) setConfig(result);
    });
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const save = async () => {
    if (config === null) return;
    const ok = await saveFn(config);
    setSavedMsg(ok ? '已提交保存' : '后端未连接，已保留本地修改');
  };

  const reset = () => {
    setConfig(cloneDefaultConfig(defaultValue));
    setSavedMsg('已恢复默认值，保存后生效');
  };

  return { config, setConfig, save, reset, savedMsg };
}
