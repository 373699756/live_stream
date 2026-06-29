interface TimeGeneralSettingsProps {
    browserSyncOnLogin: boolean;
    manualSyncAllowed: boolean;
    setBrowserSyncOnLogin: (value: boolean) => void;
    setManualSyncAllowed: (value: boolean) => void;
    setTimezone: (value: string) => void;
    timezone: string;
}

export function TimeGeneralSettings({
    browserSyncOnLogin,
    manualSyncAllowed,
    setBrowserSyncOnLogin,
    setManualSyncAllowed,
    setTimezone,
    timezone,
}: TimeGeneralSettingsProps) {
    return (
        <section className="panel">
            <div className="time-panel-heading">
                <div>
                    <h2>时间设置</h2>
                    <span>时区、手动校时和登录校时</span>
                </div>
            </div>
            <div className="time-setting-list">
                <label className="time-setting-row">
                    <span>
                        <strong>时区</strong>
                        <em>设备日志和事件时间显示使用该时区</em>
                    </span>
                    <input
                        value={timezone}
                        onChange={(event) => setTimezone(event.target.value)}
                        placeholder="Asia/Shanghai"
                    />
                </label>
                <label className="time-switch-row">
                    <input
                        type="checkbox"
                        checked={manualSyncAllowed}
                        onChange={(event) => {
                            setManualSyncAllowed(event.target.checked);
                            if (!event.target.checked) {
                                setBrowserSyncOnLogin(false);
                            }
                        }}
                    />
                    <span>
                        <strong>允许手动/浏览器校时</strong>
                        <em>关闭后，浏览器时间同步和登录后自动校时都会停止</em>
                    </span>
                </label>
                <label className="time-switch-row">
                    <input
                        type="checkbox"
                        checked={browserSyncOnLogin}
                        disabled={!manualSyncAllowed}
                        onChange={(event) =>
                            setBrowserSyncOnLogin(event.target.checked)
                        }
                    />
                    <span>
                        <strong>登录后同步浏览器时间</strong>
                        <em>账号密码登录成功后，用浏览器当前时间校准一次设备</em>
                    </span>
                </label>
            </div>
        </section>
    );
}
