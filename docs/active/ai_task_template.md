# AI Task Template

给 AI 的任务要短、具体、可验证。优先使用文件路径，不粘贴大段源码。

## Analysis Only

```text
Read AGENTS.md, docs/active/current_milestone.md,
docs/active/module_contracts.md, and docs/active/decision_log.md first.

Task:
<one concrete question or defect>

Scope:
- <modules or files to inspect>

Do not edit files.

Return only:
1. Relevant files
2. Current behavior
3. Smallest viable change
4. Risks
5. Verification command or board check
```

## Code Change

```text
Read AGENTS.md and docs/active/* first.

Task:
<one concrete implementation goal>

Problem to fix:
<specific behavior, naming, logging, or design problem>

Allowed scope:
- <files or modules allowed to change>

Forbidden scope:
- <files, modules, APIs, generated outputs, dependencies, or behavior to avoid>

Prefer:
- reuse existing interfaces
- direct readable flow
- minimal behavior change

Do not:
- refactor unrelated code
- add speculative abstraction
- add dependencies
- add runtime config files
- touch tests unless requested
- change public API/schema unless requested

Acceptance:
- <exact make/npm command, static check, or board manual check>

Commit:
<focused conventional commit message>
```

## Review

```text
Read AGENTS.md, docs/active/module_contracts.md, and the target diff/files.

Review scope:
- <commit, diff, files, or module>

Focus on:
- product behavior bugs
- lifecycle and threading risks
- module-boundary violations
- repeated wrappers or unused abstractions
- vague naming
- noisy logs
- missing verification

Return findings first, ordered by severity, with file and line references.
Do not rewrite code unless explicitly asked.
```

## Build Error Fix

```text
Build command:
<exact command>

Error excerpt:
<important compiler/linker lines only>

Task:
Fix only the root cause of this build error.

Scope:
- <likely files or module>

Do not clean up unrelated code.
Run the build command again after editing.
```

## Prompt Rules

- 一个任务只要一个具体目标。
- 超过三个模块时先拆任务。
- 实现任务必须包含验证方式。
- 先查已有接口，再新增接口。
- 每个提交只解决一个问题。
