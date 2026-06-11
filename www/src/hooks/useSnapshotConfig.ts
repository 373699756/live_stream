/**
 * useSnapshotConfig — fetch and save snapshot configuration.
 */

import { getSnapshotConfig, saveSnapshotConfig } from '../api/snapshot';
import { mockSnapshotConfig } from '../api/mockSnapshot';
import { useConfigForm } from './useConfigForm';

export function useSnapshotConfig() {
    return useConfigForm(
        getSnapshotConfig,
        saveSnapshotConfig,
        mockSnapshotConfig,
    );
}
