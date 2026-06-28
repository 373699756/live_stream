import type { MediaSessionsResponse } from '../api/types';

type MediaSessionSummary = Omit<MediaSessionsResponse, 'items'>;

interface StreamWebrtcPanelProps {
    sessionSummary: MediaSessionSummary;
}

function readyStatusText(value: boolean | undefined) {
    return value ? 'ready' : 'not ready';
}

function webrtcPortRange(sessionSummary: MediaSessionSummary) {
    const base = sessionSummary.webrtc_local_port_base ?? 0;
    const maxPeers = sessionSummary.webrtc_max_peers ?? 0;
    if (base <= 0) {
        return '--';
    }
    const end = base + Math.max(maxPeers - 1, 0);
    return end > base ? `${base}-${end}` : String(base);
}

export function StreamWebrtcPanel({ sessionSummary }: StreamWebrtcPanelProps) {
    return (
        <section className="panel wide-panel stream-webrtc-panel">
            <div className="page-heading">
                <div>
                    <h2>WebRTC 外网诊断</h2>
                    <p>公网直连依赖 HTTP/HTTPS 信令可达和 WebRTC UDP 端口可达</p>
                </div>
            </div>
            <div className="stream-webrtc-info">
                <div>
                    <span>服务</span>
                    <strong>
                        {sessionSummary.webrtc_enabled
                            ? 'enabled'
                            : 'disabled'}
                    </strong>
                </div>
                <div>
                    <span>对外地址</span>
                    <strong>{sessionSummary.webrtc_public_ip || '--'}</strong>
                </div>
                <div>
                    <span>UDP 端口</span>
                    <strong>{webrtcPortRange(sessionSummary)}</strong>
                </div>
                <div>
                    <span>ICE Servers</span>
                    <strong>{sessionSummary.webrtc_ice_servers ?? 0}</strong>
                </div>
                <div>
                    <span>Signaling</span>
                    <strong>
                        {readyStatusText(sessionSummary.webrtc_signaling_ready)}
                    </strong>
                </div>
                <div>
                    <span>ICE</span>
                    <strong>
                        {readyStatusText(sessionSummary.webrtc_ice_ready)}
                    </strong>
                </div>
                <div>
                    <span>DTLS</span>
                    <strong>
                        {readyStatusText(sessionSummary.webrtc_dtls_ready)}
                    </strong>
                </div>
                <div>
                    <span>SRTP</span>
                    <strong>
                        {readyStatusText(sessionSummary.webrtc_srtp_ready)}
                    </strong>
                </div>
                <div>
                    <span>Selected ICE</span>
                    <strong>
                        {sessionSummary.webrtc_selected_ice_pairs ?? 0}
                    </strong>
                </div>
            </div>
        </section>
    );
}
