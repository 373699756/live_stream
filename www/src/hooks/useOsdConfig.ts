/**
 * useOsdConfig — fetch and save OSD configuration.
 */

import { getOsdConfig, saveOsdConfig } from '../api/system';
import { mockOsdConfig } from '../api/mock';
import { useConfigForm } from './useConfigForm';

export function useOsdConfig() {
  return useConfigForm(getOsdConfig, saveOsdConfig, mockOsdConfig);
}
