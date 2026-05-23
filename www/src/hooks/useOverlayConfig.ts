/**
 * useOverlayConfig — fetch and save overlay configuration.
 */

import { getOverlayConfig, saveOverlayConfig } from '../api/system';
import { mockOverlayConfig } from '../api/mock';
import { useConfigForm } from './useConfigForm';

export function useOverlayConfig() {
  return useConfigForm(getOverlayConfig, saveOverlayConfig, mockOverlayConfig);
}
