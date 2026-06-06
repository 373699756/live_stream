/**
 * useConfigForm — generic hook for a load-edit-save config page.
 *
 * Handles the common pattern shared by every config page:
 *   1. Fetch config on mount via `fetchFn`.
 *   2. Expose `config` and a setter that keeps local draft state.
 *   3. `save()` calls `saveFn` and exposes saving/error state.
 *   4. `reset()` replaces draft with `defaultValue` (a deep clone).
 *
 * Usage:
 *   const { config, setConfig, save, reset, savedMsg } =
 *     useConfigForm(getVideoConfig, saveVideoConfig, mockVideoConfig);
 */

import { useEffect, useState } from 'react';
import { cloneDefaultConfig } from '../api/configDefaults';

export function useConfigForm<T>(
  fetchFn: () => Promise<T | null>,
  saveFn: (config: T) => Promise<void>,
  defaultValue: T,
) {
  const [config, setConfig] = useState<T | null>(null);
  const [savedMsg, setSavedMsg] = useState('');
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    let mounted = true;
    setLoading(true);
    void fetchFn()
      .then((result) => {
        if (!mounted) return;
        if (result !== null) {
          setConfig(result);
        }
        setError('');
      })
      .catch((err: unknown) => {
        if (mounted) {
          setError(err instanceof Error ? err.message : '加载配置失败');
        }
      })
      .finally(() => {
        if (mounted) {
          setLoading(false);
        }
      });
    return () => {
      mounted = false;
    };
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const save = async () => {
    if (config === null) return;
    setSaving(true);
    setError('');
    try {
      await saveFn(config);
      setSavedMsg('已提交保存');
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : '保存失败';
      setSavedMsg(`保存失败：${message}`);
      setError(message);
    } finally {
      setSaving(false);
    }
  };

  const reset = () => {
    setConfig(cloneDefaultConfig(defaultValue));
    setSavedMsg('已恢复默认值，保存后生效');
  };

  return { config, setConfig, save, reset, savedMsg, loading, saving, error };
}
