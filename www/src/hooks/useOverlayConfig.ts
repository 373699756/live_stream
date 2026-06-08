/**
 * useOverlayConfig — fetch and save overlay configuration.
 */

import { getOverlayConfig, saveOverlayConfig } from '../api/overlay';
import { mockOverlayConfig } from '../api/mockOverlay';
import { useConfigForm } from './useConfigForm';

export function useOverlayConfig() {
  return useConfigForm(getOverlayConfig, saveOverlayConfig, mockOverlayConfig);
}
