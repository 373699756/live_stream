import { useEffect, useRef, useState } from 'react';
import {
  api,
  closeWebrtcPeer,
  createWebrtcPeer,
  sendWebrtcCandidate,
  sendWebrtcOffer,
  snapshotUrl as buildSnapshotUrl,
} from '../api/client';
import type { StreamName, StreamStatus, WebrtcConfig } from '../api/types';
import { StatusBadge } from './StatusBadge';

interface VideoPreviewProps {
  stream: StreamName;
  statuses: StreamStatus[];
  onStreamChange: (stream: StreamName) => void;
}

export function VideoPreview({ stream, statuses, onStreamChange }: VideoPreviewProps) {
  const [mode, setMode] = useState<'webrtc' | 'snapshot'>('snapshot');
  const [snapshotTick, setSnapshotTick] = useState(0);
  const [webrtcConfig, setWebrtcConfig] = useState<WebrtcConfig | null>(null);
  const [webrtcState, setWebrtcState] = useState('等待 WebRTC 视频流');
  const surfaceRef = useRef<HTMLDivElement | null>(null);
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const peerRef = useRef<RTCPeerConnection | null>(null);
  const peerIdRef = useRef('');
  const active = statuses.find((item) => item.stream === stream);
  const snapshotUrl = buildSnapshotUrl(stream, snapshotTick);

  useEffect(() => {
    let mounted = true;
    void api.getWebrtcConfig()
      .then((config) => {
        if (mounted) {
          setWebrtcConfig(config);
          if (!config.enabled) {
            setMode('snapshot');
            setWebrtcState('WebRTC 未启用');
          }
        }
      })
      .catch(() => {
        if (mounted) {
          setWebrtcConfig(null);
          setMode('snapshot');
          setWebrtcState('WebRTC 配置不可用');
        }
      });
    return () => {
      mounted = false;
    };
  }, []);

  const webrtcEnabled = Boolean(webrtcConfig?.enabled);

  useEffect(() => {
    if (mode !== 'snapshot') {
      return;
    }
    const timer = window.setInterval(() => setSnapshotTick((value) => value + 1), 2000);
    return () => window.clearInterval(timer);
  }, [mode]);

  useEffect(() => {
    if (mode === 'webrtc' && !webrtcEnabled) {
      setMode('snapshot');
      setWebrtcState('WebRTC 未启用');
      return;
    }
    if (mode !== 'webrtc') {
      if (peerRef.current) {
        peerRef.current.close();
        peerRef.current = null;
      }
      if (videoRef.current) {
        videoRef.current.srcObject = null;
      }
      void closeWebrtcPeer(peerIdRef.current);
      peerIdRef.current = '';
      setWebrtcState('等待 WebRTC 视频流');
      return;
    }

    let closed = false;
    const pc = new RTCPeerConnection({
      iceServers: (webrtcConfig?.ice_servers || []).map((server) => ({
        urls: server.url,
        username: server.username,
        credential: server.credential,
      })),
    });
    peerRef.current = pc;
    pc.addTransceiver('video', { direction: 'recvonly' });
    pc.ontrack = (event) => {
      if (videoRef.current && event.track.kind === 'video') {
        videoRef.current.srcObject = new MediaStream([event.track]);
        setWebrtcState('视频已连接');
      }
    };
    pc.onicecandidate = (event) => {
      if (event.candidate && peerIdRef.current) {
        void sendWebrtcCandidate(peerIdRef.current, event.candidate.toJSON());
      }
    };
    pc.onconnectionstatechange = () => {
      setWebrtcState(pc.connectionState);
    };
    pc.oniceconnectionstatechange = () => {
      if (pc.iceConnectionState === 'failed') {
        setWebrtcState('ICE 连接失败');
      }
    };

    void (async () => {
      try {
        const peer = await createWebrtcPeer(stream);
        if (!peer.peer_id || closed) {
          setWebrtcState('WebRTC 后端不可用');
          return;
        }
        peerIdRef.current = peer.peer_id;
        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        const answer = await sendWebrtcOffer(peer.peer_id, offer.sdp || '');
        if (!answer.sdp || closed) {
          setWebrtcState('WebRTC 应答无效');
          return;
        }
        await pc.setRemoteDescription({ type: 'answer', sdp: answer.sdp });
      } catch {
        setWebrtcState('WebRTC 连接失败');
      }
    })();

    return () => {
      closed = true;
      pc.close();
      peerRef.current = null;
      if (videoRef.current) {
        videoRef.current.srcObject = null;
      }
      void closeWebrtcPeer(peerIdRef.current);
      peerIdRef.current = '';
    };
  }, [mode, stream, webrtcConfig, webrtcEnabled]);

  const openSnapshot = () => {
    window.open(buildSnapshotUrl(stream), '_blank', 'noopener,noreferrer');
  };

  const requestFullscreen = () => {
    void surfaceRef.current?.requestFullscreen?.();
  };

  return (
    <section className="preview-panel">
      <div className="preview-toolbar">
        <div className="segmented">
          <button
            type="button"
            className={stream === 'main' ? 'active' : ''}
            onClick={() => onStreamChange('main')}
          >
            主码流
          </button>
          <button
            type="button"
            className={stream === 'sub' ? 'active' : ''}
            onClick={() => onStreamChange('sub')}
          >
            子码流
          </button>
        </div>
        <div className="preview-actions">
          <button
            type="button"
            className={mode === 'webrtc' ? 'active' : ''}
            disabled={!webrtcEnabled}
            onClick={() => setMode('webrtc')}
          >
            WebRTC
          </button>
          <button
            type="button"
            className={mode === 'snapshot' ? 'active' : ''}
            onClick={() => setMode('snapshot')}
          >
            抓图预览
          </button>
          <button type="button" onClick={openSnapshot}>截图</button>
          <button type="button" onClick={requestFullscreen}>全屏</button>
        </div>
      </div>

      <div className="video-surface" ref={surfaceRef}>
        {mode === 'webrtc' ? (
          <video ref={videoRef} className="video-element" autoPlay muted playsInline />
        ) : (
          <img
            className="snapshot-preview"
            src={snapshotUrl}
            alt="snapshot preview"
            onLoad={(event) => {
              event.currentTarget.style.opacity = '1';
            }}
            onError={(event) => {
              event.currentTarget.style.opacity = '0';
            }}
          />
        )}
        <div className="video-placeholder">
          <div className="lens-ring" />
          <strong>{mode === 'webrtc' ? webrtcState : '抓图预览'}</strong>
          <span>{mode === 'webrtc' ? '正在建立浏览器拉流会话' : '定时刷新 JPEG 抓图'}</span>
        </div>
      </div>

      <div className="preview-footer">
        <StatusBadge state={active?.state === 'running' ? 'running' : 'pending'} />
        <span>{active?.codec || 'H.264'}</span>
        <span>分辨率 {active?.resolution || '1920x1080'}</span>
        <span>{active?.fps || 25} fps</span>
        <span>{active?.bitrateKbps || 4096} kbps</span>
      </div>
    </section>
  );
}
