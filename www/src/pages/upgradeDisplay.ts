import type { UpgradeInfo } from '../api/types';

export type UpgradeDisplayState =
    | 'idle'
    | 'checking'
    | 'preparing'
    | 'writing'
    | 'committing'
    | 'waiting_reboot'
    | 'rebooting'
    | 'completed'
    | 'failed'
    | 'canceled';

export type UpgradeNoteTone = 'neutral' | 'info' | 'success' | 'warning' | 'error';

export interface UpgradeDisplayInfo {
    state: UpgradeDisplayState;
    stateLabel: string;
    stageLabel: string;
    note: string;
    tone: UpgradeNoteTone;
    progressTone: UpgradeNoteTone;
    isActive: boolean;
}

function normalizedStage(status: UpgradeInfo) {
    return status.current_stage.trim().toLowerCase();
}

function isRebootStage(status: UpgradeInfo) {
    const stage = normalizedStage(status);
    return stage.includes('reboot') || stage.includes('restart');
}

function isHelperStage(status: UpgradeInfo) {
    return normalizedStage(status).includes('sysupgrade helper');
}

export function formatUpgradeStageDetail(status: UpgradeInfo) {
    const stage = normalizedStage(status);
    if (!stage) {
        return '-';
    }
    if (stage.includes('checking temporary upgrade workspace')) {
        return '正在检查临时写入环境';
    }
    if (stage.includes('checking flash partition layout')) {
        return '正在检查 Flash 分区布局';
    }
    if (stage.includes('creating temporary write workspace')) {
        return '正在创建临时写入目录';
    }
    if (stage.includes('extracting upgrade package')) {
        return '正在解包升级镜像';
    }
    if (stage.includes('writing flash bin')) {
        return '正在写入应用分区';
    }
    if (stage.includes('writing flash web')) {
        return '正在写入 Web 分区';
    }
    if (stage.includes('writing flash config')) {
        return '正在写入配置分区';
    }
    if (stage.includes('erasing mtd partition')) {
        return '正在擦除 Flash 分区';
    }
    if (stage.includes('writing mtd image')) {
        return '正在写入 Flash 镜像';
    }
    if (stage.includes('verifying mtd readback')) {
        return '正在校验 Flash 回读数据';
    }
    if (stage.includes('waiting reboot')) {
        return '等待确认重启';
    }
    if (stage.includes('rebooting')) {
        return '正在重启设备';
    }
    if (stage.includes('upgrade completed') || stage === 'completed') {
        return '升级流程完成';
    }
    return status.current_stage || '-';
}

function displayState(status: UpgradeInfo): UpgradeDisplayState {
    if (status.state === 'failed') {
        return 'failed';
    }
    if (status.state === 'canceled') {
        return 'canceled';
    }
    if (status.state === 'idle') {
        return 'idle';
    }
    if (status.state === 'validating') {
        return 'checking';
    }
    if (status.state === 'preparing') {
        return 'preparing';
    }
    if (status.state === 'writing') {
        return 'writing';
    }
    if (status.state === 'committing') {
        return 'committing';
    }
    if (status.state === 'waiting_reboot') {
        return 'waiting_reboot';
    }
    if (status.state === 'completed' && isRebootStage(status)) {
        return 'rebooting';
    }
    return 'completed';
}

function stateLabel(state: UpgradeDisplayState) {
    switch (state) {
        case 'idle':
            return '空闲';
        case 'checking':
            return '校验中';
        case 'preparing':
            return '准备写入';
        case 'writing':
            return '写入中';
        case 'committing':
            return '提交中';
        case 'waiting_reboot':
            return '等待重启';
        case 'rebooting':
            return '重启中';
        case 'completed':
            return '已完成';
        case 'failed':
            return '失败';
        case 'canceled':
            return '已取消';
    }
}

function stageLabel(status: UpgradeInfo, state: UpgradeDisplayState) {
    if (state === 'writing' && isHelperStage(status)) {
        return '系统升级接管中';
    }
    switch (state) {
        case 'idle':
            return '未开始';
        case 'checking':
            return '升级包校验中';
        case 'preparing':
            return '创建临时写入环境';
        case 'writing':
            return 'Flash 写入中';
        case 'committing':
            return '写入结果提交中';
        case 'waiting_reboot':
            return '等待用户确认重启';
        case 'rebooting':
            return '设备重启恢复中';
        case 'completed':
            return '升级流程完成';
        case 'failed':
            return '升级失败';
        case 'canceled':
            return '升级已取消';
    }
}

function note(status: UpgradeInfo, state: UpgradeDisplayState) {
    if (state === 'failed') {
        return status.error_message || '升级失败，请查看 /data/upgrade.log。';
    }
    if (state === 'checking') {
        return '正在校验升级包格式、签名、版本和目标型号。';
    }
    if (state === 'preparing') {
        return '正在准备临时写入环境，尚未进入 Flash 擦写。';
    }
    if (state === 'writing' && isHelperStage(status)) {
        return '系统升级 helper 已接管写入流程，Web API 可能短暂断开；请等待设备重启恢复。';
    }
    if (state === 'writing') {
        return '正在擦写 Flash，请保持供电稳定，不要断电。';
    }
    if (state === 'committing') {
        return 'Flash 写入已完成，正在提交升级结果，请勿断电。';
    }
    if (state === 'waiting_reboot') {
        return '升级已写入，需要确认重启后生效。提交和等待重启阶段不可取消。';
    }
    if (state === 'rebooting') {
        return '重启已触发，设备和 Web 服务会短暂不可达，恢复后再检查运行状态。';
    }
    if (state === 'completed') {
        return '升级已完成，设备已返回可查询状态。';
    }
    if (state === 'canceled') {
        return '升级任务已取消，Flash 写入未开始或已按后端允许阶段停止。';
    }
    return '';
}

function noteTone(state: UpgradeDisplayState): UpgradeNoteTone {
    if (state === 'failed') {
        return 'error';
    }
    if (state === 'completed') {
        return 'success';
    }
    if (state === 'idle' || state === 'canceled') {
        return 'neutral';
    }
    if (state === 'checking' || state === 'preparing' || state === 'rebooting') {
        return 'info';
    }
    return 'warning';
}

export function buildUpgradeDisplayInfo(
    status: UpgradeInfo,
): UpgradeDisplayInfo {
    const state = displayState(status);
    const tone = noteTone(state);
    return {
        state,
        stateLabel: stateLabel(state),
        stageLabel: stageLabel(status, state),
        note: note(status, state),
        tone,
        progressTone: state === 'failed' ? 'error' : tone,
        isActive:
            state !== 'idle' &&
            state !== 'completed' &&
            state !== 'failed' &&
            state !== 'canceled',
    };
}
