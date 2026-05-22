# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

实现首次登录强制改密，去掉 `configs/auth_users.json` 明文密码，并把 auth 用户文件
读写收敛到 `config_service` 提供的 auth user store 接口。

## Problem fixed

原先 `auth_service` 允许 `password_plaintext` 作为后备校验，配置里保留明文密码；
Web 登录也没有首次改密流程。RTSP/ONVIF 通过 Basic auth 直接调用登录，如果不同步
处理，会绕过 Web 的首次改密约束。

## Files changed

主要变更：

- `auth_service` 增加 `must_change_password`、`ChangePassword` 和用户 store
  `UpdatePassword`；登录只接受 hashed credential。
- `config_service` 新增 `CreateConfigAuthUserStore()`，负责加载、校验、保存
  `configs/auth_users.json`，拒绝 `password`/`password_plaintext` 字段。
- HTTP auth API 增加 `POST /api/auth/change-password`，`login/me` 返回
  `must_change_password`；未改密用户只能访问 `me/logout/change-password`。
- Web Console 登录后如果需要改密，进入强制改密页，改完后才显示管理台。
- RTSP/ONVIF Basic auth 对 `must_change_password` 用户返回拒绝，并清理临时
  session。
- 默认配置改为测试友好的密码策略：最小 1 位，不要求数字。

## Behavior changed

- 初始 `admin/admin` 可登录，但会返回 `must_change_password=true`。
- 改密成功后 `configs/auth_users.json` 只保存新的 SHA-256 salted credential，
  `must_change_password` 变为 false。
- 改密前 HTTP 管理 API 返回 `403 {"error":"must_change_password"}`；RTSP/ONVIF
  也拒绝预览/管理认证。
- 同一用户改密成功后，只保留当前 session，其他旧 session 被清理。

## Verification

通过：

- `make -C libs/auth_service`
- `make -C libs/config_service`
- `make -C libs/http_service`
- `make -C libs/rtsp_service`
- `make -C libs/onvif_service`
- `npm run build` in `www/`
- `git diff --check`

`make -j2` 已跑到最终链接阶段，失败原因是缺少第三方静态库：
`libmetartc8.a`、`libmetartccore8.a`、`libyangutil8.a`、`libusrsctp.a`。

## Commit

Pending.

## Deviations

未迁移或修复旧测试源码。当前测试源码仍使用旧的 service API 形态，和本任务生产
代码改动无关。

## Blocked or follow-up

如果要恢复整工程最终链接，需要先补齐 `3rdparty/install/lib/` 下缺失的 metaRTC 和
usrsctp 静态库。
