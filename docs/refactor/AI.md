AI 后端职责过厚
ai_runtime.cpp (line 160) 的 AiRuntime::State 同时管配置热更新、任务生命周期、采集、推理、告警注入、快照和统计；nnie_engine.cpp (line 248) 也把模型加载、workspace、VGS/IVE 转换、SSD 后处理都放在一个 1500 行文件中。建议拆成：任务调度/运行态、告警输出、NNIE workspace、输入转换、SSD 解码几个明确文件。

AI 告警前端页面已经到了该拆的时候
AiAlertsPage.tsx (line 455) 是 1000+ 行，并且项目里已有 features/ai-alerts (line 30) 组件，但当前路由仍直接加载大页面：App.tsx (line 9)。建议把页面改成组合层，复用 AiRuntimeSummary/AiPerimeterEditor/AiEventTaskPanel/AiAlertGrid，删除重复逻辑。
