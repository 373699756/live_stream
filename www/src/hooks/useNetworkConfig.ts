/**
 * useNetworkConfig — fetch and save network configuration.
 *
 * Wraps the load-edit-save pattern for NetworkConfigPage.
 */

import { getNetworkConfig, saveNetworkConfig } from '../api/network';
import { mockNetworkConfig } from '../api/mock';
import { useConfigForm } from './useConfigForm';

export function useNetworkConfig() {
  return useConfigForm(getNetworkConfig, saveNetworkConfig, mockNetworkConfig);
}
