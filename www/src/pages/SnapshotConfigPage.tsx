import { useSnapshotConfig } from '../hooks/useSnapshotConfig';
import { ConfigActionBar } from '../components/ConfigActionBar';
import { FormField } from '../components/FormField';

export function SnapshotConfigPage() {
    const { config, setConfig, save, reset, savedMsg, loading, saving, error } =
        useSnapshotConfig();

    if (loading) {
        return <div className="panel">加载抓图配置...</div>;
    }
    if (!config) {
        return (
            <div className="panel">
                抓图配置加载失败：{error || '无可用配置'}
            </div>
        );
    }

    return (
        <div className="config-preview-layout">
            <section className="panel settings-column">
                <div className="page-heading">
                    <div>
                        <h2>抓图参数</h2>
                        <p>
                            配置 JPEG 生成参数，HTTP/ONVIF
                            抓图入口由后端固定生成。
                        </p>
                    </div>
                </div>

                <div className="form-grid form-grid-single">
                    <FormField label="启用抓图">
                        <input
                            type="checkbox"
                            checked={config.enabled}
                            onChange={(event) =>
                                setConfig({
                                    ...config,
                                    enabled: event.target.checked,
                                })
                            }
                        />
                    </FormField>
                    <FormField label="JPEG 质量">
                        <input
                            type="number"
                            min="1"
                            max="100"
                            value={config.jpeg_quality}
                            onChange={(event) =>
                                setConfig({
                                    ...config,
                                    jpeg_quality: Number(event.target.value),
                                })
                            }
                        />
                    </FormField>
                    <FormField label="超时 ms">
                        <input
                            type="number"
                            min="100"
                            value={config.timeout_ms}
                            onChange={(event) =>
                                setConfig({
                                    ...config,
                                    timeout_ms: Number(event.target.value),
                                })
                            }
                        />
                    </FormField>
                </div>

                <ConfigActionBar
                    error={error}
                    message={savedMsg}
                    onReset={reset}
                    onSave={() => void save()}
                    saving={saving}
                />
            </section>
        </div>
    );
}
