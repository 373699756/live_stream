/**
 * useSnapshotConfig — fetch and save snapshot configuration.
 */

import { getSnapshotConfig, saveSnapshotConfig } from '../api/snapshot';
import { mockSnapshotConfig } from '../api/mock';
import { useConfigForm } from './useConfigForm';

export function useSnapshotConfig() {
  return useConfigForm(getSnapshotConfig, saveSnapshotConfig, mockSnapshotConfig);
}
