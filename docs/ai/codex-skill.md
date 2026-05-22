# live-stream-codex Skill Draft

这是本工程专用 skill 草案。它不是高频必读文档；只有在需要把项目规则迁移到
Codex skill、整理 AI 工作流、或跨会话复用本工程经验时才读取。

推荐安装名：`live-stream-codex`

## SKILL.md

```markdown
---
name: live-stream-codex
description: Use for coding, review, refactor, architecture cleanup, or frontend work in the live_stream embedded IPC video preview project. Applies module boundaries, token-aware context loading, C++/React conventions, streaming state ownership, and anti-overengineering rules.
---

# live_stream Codex Skill

## Read Order

1. Always read `AGENTS.md`.
2. For normal implementation/review, read only:
   - `docs/active/current_milestone.md`
   - `docs/active/module_contracts.md`
   - `docs/active/decision_log.md`
3. Read long references only when relevant:
   - Architecture: `docs/architecture/overview.md`
   - Module ownership: `docs/architecture/module-boundaries.md`
   - API/config schema: `docs/contracts/api-config.md`
   - Events: `docs/contracts/event-payloads.md`
   - Lessons learned: `docs/ai/lessons-learned.md`

## Core Rules

- Keep tasks narrow: one module plus at most one adjacent interface module.
- Reuse existing interfaces before adding helpers, classes, hooks, or DTOs.
- Prefer direct readable flow over Context/Manager/State/Store abstractions.
- Do not add audio, recording, storage playback, or related UI/API.
- Do not touch `3rdparty/`, generated outputs, or tests unless requested.
- Frontend consumes backend ready/status fields; it does not guess device state.
- Frame paths must avoid frequent logs, allocations, string formatting, and broad locks.
- Bugfix, cleanup, rename, and refactor must be separate changes.

## Module Ownership

- `app/`: composition root, runtime paths, service wiring, startup/shutdown.
- `media_service`: video pipeline, MPP/VENC/ISP, stream lifecycle, key frames.
- `stream_hub_service`: encoded-frame fanout, HLS/FLV state, browser stream readiness.
- `http_service`: HTTP routing, auth boundary, DTO mapping, static UI, protocol endpoints.
- `webrtc_service`: WebRTC peer/session, SDP/ICE, media transport.
- `www/`: IPC/NVR UI, API consumption, mock fallback, playback lifecycle.

## Task Workflow

1. Recover context with `git status --short` and `git log --oneline -5`.
2. State goal, allowed scope, forbidden scope, and verification.
3. Inspect relevant interfaces before editing.
4. Make the smallest viable change.
5. Run targeted verification.
6. Commit only focused files when requested or when project rules require it.

## Review Checklist

- Does the change reuse existing API/state owners?
- Did it add an abstraction that can be deleted?
- Are names business-specific rather than generic?
- Are lifecycle and failure paths visible and linear?
- Are logs low-frequency and useful?
- Did frontend/backend DTO and docs stay consistent?
- Is verification proportional to risk?
```

## 安装建议

如果要安装成全局 Codex skill，应放到：

```text
$CODEX_HOME/skills/live-stream-codex/SKILL.md
```

当前先保留为仓库文档，避免全局 skill 污染其他项目。需要跨会话自动触发时再安装。
