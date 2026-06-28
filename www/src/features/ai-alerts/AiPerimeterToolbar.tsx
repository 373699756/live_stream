import type { AiPerimeterRegion } from '../../api/types';
import { formatPercent } from './aiConfigDraft';

interface AiPerimeterToolbarProps {
    activeRegion: AiPerimeterRegion | null;
    activeRegionIndex: number;
    addRegion: () => void;
    clearRegions: () => void;
    deleteRegion: () => void;
    perimeterRegions: AiPerimeterRegion[];
    selectRegion: (index: number) => void;
}

export function AiPerimeterToolbar({
    activeRegion,
    activeRegionIndex,
    addRegion,
    clearRegions,
    deleteRegion,
    perimeterRegions,
    selectRegion,
}: AiPerimeterToolbarProps) {
    return (
        <div className="ai-perimeter-toolbar">
            <div>
                <strong>周界区域</strong>
                <span>
                    {perimeterRegions.length > 0
                        ? `${perimeterRegions.length} 个区域`
                        : '整幅画面'}
                </span>
                {activeRegion ? (
                    <em>
                        当前 {activeRegionIndex + 1}: x{' '}
                        {formatPercent(activeRegion.x)} / y{' '}
                        {formatPercent(activeRegion.y)} / w{' '}
                        {formatPercent(activeRegion.width)} / h{' '}
                        {formatPercent(activeRegion.height)}
                    </em>
                ) : null}
                {perimeterRegions.length > 0 ? (
                    <div
                        className="ai-perimeter-region-list"
                        aria-label="周界区域"
                    >
                        {perimeterRegions.map((region, index) => (
                            <button
                                type="button"
                                className={
                                    activeRegionIndex === index ? 'active' : ''
                                }
                                key={`${region.name}-${index}`}
                                onClick={() => selectRegion(index)}
                            >
                                {index + 1}
                            </button>
                        ))}
                    </div>
                ) : null}
            </div>
            <div className="ai-perimeter-actions">
                <button type="button" onClick={addRegion}>
                    新增区域
                </button>
                <button
                    type="button"
                    disabled={!activeRegion}
                    onClick={deleteRegion}
                >
                    删除当前
                </button>
                <button
                    type="button"
                    disabled={perimeterRegions.length === 0}
                    onClick={clearRegions}
                >
                    清空
                </button>
            </div>
        </div>
    );
}
