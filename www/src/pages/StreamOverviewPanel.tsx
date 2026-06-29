import type { MediaStreamInfo } from '../api/types';
import { StatusBadge } from '../components/StatusBadge';
import { previewValueText } from '../components/previewDisplay';
import {
    findStreamInfo,
    previewStreams,
    streamLabel,
} from './streamInfoNames';

interface StreamOverviewPanelProps {
    streamInfos: MediaStreamInfo[];
}

export function StreamOverviewPanel({
    streamInfos,
}: StreamOverviewPanelProps) {
    return (
        <section className="panel wide-panel stream-info-panel">
            <div className="page-heading">
                <div>
                    <h2>运行总览</h2>
                    <p>主/子码流运行态、subscription/client 和缓存状态</p>
                </div>
            </div>
            <div className="stream-info-cards">
                {previewStreams.map((stream) => {
                    const streamInfo = findStreamInfo(streamInfos, stream);
                    if (!streamInfo) {
                        return (
                            <article
                                className="stream-info-card unavailable"
                                key={stream}
                            >
                                <div>
                                    <h3>{streamLabel(stream)}</h3>
                                    <StatusBadge
                                        state="error"
                                        label="运行态不可用"
                                    />
                                </div>
                                <span>后端未返回该码流运行信息</span>
                            </article>
                        );
                    }
                    return (
                        <article className="stream-info-card" key={stream}>
                            <div>
                                <h3>{streamLabel(stream)}</h3>
                                <StatusBadge
                                    state={
                                        streamInfo.running
                                            ? 'running'
                                            : 'pending'
                                    }
                                    label={
                                        streamInfo.running ? '运行中' : '未运行'
                                    }
                                />
                            </div>
                            <dl>
                                <div>
                                    <dt>编码</dt>
                                    <dd>
                                        {previewValueText(streamInfo.codec)}
                                    </dd>
                                </div>
                                <div>
                                    <dt>分辨率</dt>
                                    <dd>
                                        {previewValueText(
                                            streamInfo.resolution,
                                            '--',
                                        )}
                                    </dd>
                                </div>
                                <div>
                                    <dt>帧率</dt>
                                    <dd>
                                        {previewValueText(
                                            streamInfo.fps,
                                            '--',
                                        )}{' '}
                                        fps
                                    </dd>
                                </div>
                                <div>
                                    <dt>码率</dt>
                                    <dd>
                                        {previewValueText(
                                            streamInfo.bitrate_kbps,
                                            '--',
                                        )}{' '}
                                        kbps
                                    </dd>
                                </div>
                                <div>
                                    <dt>读者/客户端</dt>
                                    <dd>
                                        {streamInfo.active_subscriptions} /{' '}
                                        {streamInfo.preview_clients}
                                    </dd>
                                </div>
                                <div>
                                    <dt>缓存</dt>
                                    <dd>
                                        {streamInfo.cached_frames} 帧 /{' '}
                                        {streamInfo.cached_bytes} B
                                    </dd>
                                </div>
                                <div>
                                    <dt>最近 DTS</dt>
                                    <dd>
                                        {previewValueText(
                                            streamInfo.last_dts,
                                            '--',
                                        )}
                                    </dd>
                                </div>
                            </dl>
                        </article>
                    );
                })}
            </div>
        </section>
    );
}
