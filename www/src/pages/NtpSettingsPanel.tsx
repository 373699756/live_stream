import { useMemo } from 'react';

interface NtpSettingsPanelProps {
    busy: boolean;
    ntpEnabled: boolean;
    ntpIntervalSec: number;
    ntpServersText: string;
    setNtpEnabled: (value: boolean) => void;
    setNtpIntervalSec: (value: number) => void;
    setNtpServersText: (value: string) => void;
    syncNtp: () => void;
}

const commonNtpServers = [
    'pool.ntp.org',
    'cn.pool.ntp.org',
    'ntp.aliyun.com',
    'time.cloudflare.com',
];

function ntpServerRows(text: string) {
    const rows = text.split(/\r?\n/);
    if (rows.length === 0 || rows.every((row) => row.trim().length === 0)) {
        return [''];
    }
    return rows;
}

function joinNtpServerRows(rows: string[]) {
    return rows.join('\n');
}

export function NtpSettingsPanel({
    busy,
    ntpEnabled,
    ntpIntervalSec,
    ntpServersText,
    setNtpEnabled,
    setNtpIntervalSec,
    setNtpServersText,
    syncNtp,
}: NtpSettingsPanelProps) {
    const ntpServers = useMemo(
        () => ntpServerRows(ntpServersText),
        [ntpServersText],
    );
    const activeNtpServerTotal = ntpServers.filter(
        (server) => server.trim().length > 0,
    ).length;

    const updateNtpServer = (index: number, value: string) => {
        const nextServers = [...ntpServers];
        nextServers[index] = value;
        setNtpServersText(joinNtpServerRows(nextServers));
    };

    const addNtpServer = (server = '') => {
        const nextServers =
            ntpServers.length === 1 && ntpServers[0].trim() === ''
                ? [server]
                : [...ntpServers, server];
        setNtpServersText(joinNtpServerRows(nextServers));
    };

    const removeNtpServer = (index: number) => {
        const nextServers = ntpServers.filter(
            (_, rowIndex) => rowIndex !== index,
        );
        setNtpServersText(
            joinNtpServerRows(nextServers.length > 0 ? nextServers : ['']),
        );
    };

    const addCommonNtpServer = (server: string) => {
        const currentServers = ntpServers.map((item) => item.trim());
        if (currentServers.includes(server)) {
            return;
        }
        addNtpServer(server);
    };

    return (
        <section className="panel">
            <div className="time-panel-heading">
                <div>
                    <h2>NTP 设置</h2>
                    <span>{activeNtpServerTotal} 个服务器地址</span>
                </div>
                <button
                    type="button"
                    onClick={syncNtp}
                    disabled={busy || !ntpEnabled}
                >
                    立即同步
                </button>
            </div>
            <div className="time-setting-list">
                <label className="time-switch-row">
                    <input
                        type="checkbox"
                        checked={ntpEnabled}
                        onChange={(event) =>
                            setNtpEnabled(event.target.checked)
                        }
                    />
                    <span>
                        <strong>启用 NTP</strong>
                        <em>按同步间隔自动从服务器校准设备时间</em>
                    </span>
                </label>
                <label className="time-setting-row">
                    <span>
                        <strong>同步间隔</strong>
                        <em>单位：秒</em>
                    </span>
                    <input
                        type="number"
                        min={1}
                        value={ntpIntervalSec}
                        onChange={(event) =>
                            setNtpIntervalSec(Number(event.target.value))
                        }
                    />
                </label>
                <div className="time-server-editor">
                    <div className="time-server-editor-heading">
                        <span>
                            <strong>NTP 服务器地址</strong>
                            <em>按优先级从上到下尝试，支持域名或 IP</em>
                        </span>
                        <button type="button" onClick={() => addNtpServer()}>
                            新增地址
                        </button>
                    </div>
                    <div className="time-server-list">
                        {ntpServers.map((server, index) => (
                            <div className="time-server-row" key={index}>
                                <strong>{index + 1}</strong>
                                <input
                                    value={server}
                                    onChange={(event) =>
                                        updateNtpServer(
                                            index,
                                            event.target.value,
                                        )
                                    }
                                    placeholder="pool.ntp.org"
                                />
                                <button
                                    type="button"
                                    disabled={
                                        ntpServers.length === 1 &&
                                        server.trim() === ''
                                    }
                                    onClick={() => removeNtpServer(index)}
                                >
                                    删除
                                </button>
                            </div>
                        ))}
                    </div>
                    <div className="time-server-presets">
                        {commonNtpServers.map((server) => (
                            <button
                                type="button"
                                key={server}
                                disabled={ntpServers
                                    .map((item) => item.trim())
                                    .includes(server)}
                                onClick={() => addCommonNtpServer(server)}
                            >
                                {server}
                            </button>
                        ))}
                    </div>
                </div>
            </div>
        </section>
    );
}
