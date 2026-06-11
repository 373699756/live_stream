import type {
    AiAlertRecord,
    AiBackendId,
    AiStatus,
    AiTaskName,
} from '../../api/types';

export interface AiEventTab {
    task: AiTaskName;
    label: string;
    title: string;
    emptyTitle: string;
    emptyText: string;
}

export const kAiEventTabs: AiEventTab[] = [
    {
        task: 'occlusion_detection',
        label: '遮挡',
        title: '遮挡抓拍',
        emptyTitle: '暂无遮挡抓拍',
        emptyText: '当前没有镜头遮挡抓拍记录。',
    },
    {
        task: 'perimeter_detection',
        label: '周界',
        title: '周界抓拍',
        emptyTitle: '暂无周界抓拍',
        emptyText: '当前没有周界入侵抓拍记录。',
    },
    {
        task: 'motion_classification',
        label: '移动',
        title: '移动抓拍',
        emptyTitle: '暂无移动抓拍',
        emptyText: '当前没有画面移动抓拍记录。',
    },
    {
        task: 'object_detection',
        label: '目标',
        title: '目标抓拍',
        emptyTitle: '暂无目标抓拍',
        emptyText: '当前没有目标检测抓拍记录。',
    },
];

export function taskLabel(task: AiTaskName) {
    switch (task) {
        case 'perimeter_detection':
            return '周界检测';
        case 'motion_classification':
            return '移动侦测';
        case 'occlusion_detection':
            return '遮挡检测';
        case 'object_detection':
            return '目标检测';
    }
}

export function taskDescription(task: AiTaskName) {
    switch (task) {
        case 'perimeter_detection':
            return '目标进入周界区域后生成抓拍，并注入 AI 告警输入。';
        case 'motion_classification':
            return '画面出现有效移动后生成抓拍，并注入 AI 告警输入。';
        case 'occlusion_detection':
            return '镜头被遮挡或画面异常后生成抓拍，并注入 AI 告警输入。';
        case 'object_detection':
            return '检测到人员、车辆等目标后生成抓拍，并注入 AI 告警输入。';
    }
}

export function taskCaptureScope(task: AiTaskName) {
    switch (task) {
        case 'perimeter_detection':
            return '周界抓拍保存的是进入区域的目标，卡片上的 person/vehicle 是模型识别出的目标类别。';
        case 'motion_classification':
            return '移动抓拍保存的是画面移动触发的快照。';
        case 'occlusion_detection':
            return '遮挡抓拍保存的是镜头遮挡或画面异常触发的快照。';
        case 'object_detection':
            return '目标抓拍保存的是人员、车辆等目标检测快照。';
    }
}

export function taskUsesModel(task: AiTaskName) {
    return task === 'object_detection' || task === 'perimeter_detection';
}

export function taskRequiresModelPath(task: AiTaskName, backend: AiBackendId) {
    return backend === 'hisi3516dv300_nnie' && taskUsesModel(task);
}

export function alertGroupsByTask(alerts: AiAlertRecord[]) {
    const groups: Record<AiTaskName, AiAlertRecord[]> = {
        object_detection: [],
        perimeter_detection: [],
        motion_classification: [],
        occlusion_detection: [],
    };
    alerts.forEach((alert) => {
        groups[alert.task].push(alert);
    });
    return groups;
}

export function alertsForTask(
    alertGroups: Record<AiTaskName, AiAlertRecord[]>,
    task: AiTaskName,
) {
    return alertGroups[task].slice(0, 10);
}

export function tabStateLabel(status: AiStatus | null, task: AiTaskName) {
    if (!status) {
        return '读取中';
    }
    const taskStatus = status.tasks.find((item) => item.config.task === task);
    if (!taskStatus?.config.enabled) {
        return '未运行';
    }
    if (!taskStatus.stats.enabled) {
        return '未运行';
    }
    return taskStatus.stats.backend_available ? '当前运行' : '后端异常';
}

export function emptyTextForTask(
    status: AiStatus | null,
    activeTab: AiEventTab,
    activeTask: AiTaskName,
) {
    const taskStatus = status?.tasks.find(
        (item) => item.config.task === activeTask,
    );
    if (status && !taskStatus?.config.enabled) {
        return `${taskLabel(activeTask)} 未启用，开启后才会生成新的抓拍。`;
    }
    if (
        status &&
        taskStatus?.config.enabled &&
        taskStatus.stats.enabled &&
        !taskStatus.stats.backend_available
    ) {
        return '推理后端当前不可用。';
    }
    return activeTab.emptyText;
}
