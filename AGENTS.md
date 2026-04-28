# live_stream Codex 项目说明

## 项目结构

- `app/` 是 C++ 程序入口，当前主入口为 `app/main.cpp`。
- `libs/*_service/` 是服务模块目录，通常包含 `include/`、`src/`、
  `Makefile` 和 `module.mk`。
- `libs/service_rules.mk` 定义服务模块的通用构建规则。
- `configs/` 存放运行配置 JSON，例如业务配置和认证用户配置。
- `www/` 是 IPC Web Console，技术栈为 Vite、React、TypeScript 和
  plain CSS/CSS variables。
- `3rdparty/` 是第三方依赖区域，`build/` 是构建产物区域。除非任务明确要求，
  不要改动这些目录中的内容。

## 常用命令

在仓库根目录：

```sh
make -j2
make clean
```

构建单个服务模块：

```sh
make -C libs/<service>
```

前端开发和构建在 `www/` 目录下执行：

```sh
npm run dev
npm run build
```

## C++ 约定

- 项目使用 C++17，构建开启 `-Wall -Wextra -Werror`。
- 不使用 C++ exceptions 和 RTTI；错误处理优先使用 `infra::Status` 或
  `infra::Result`。
- 保持现有 Google-like 风格：
  - 缩进使用 2 个空格。
  - 类型名使用 `PascalCase`。
  - 函数名使用 `PascalCase`。
  - 变量和参数使用 `snake_case`。
  - 常量使用 `kName` 风格。
  - class 成员变量使用尾随下划线，例如 `logger_`。
- 新增 public header 时保持自包含，并使用
  `LIVE_STREAM_<MODULE>_<FILE>_H_` 形式的 include guard。
- include 顺序遵循现有代码习惯：相关项目头、C/C++ 标准库、其他项目头。
- 避免引入宏技巧、隐式所有权或过度抽象。所有权、生命周期、线程假设不明显时，
  需要在接口或实现附近说明。

## 模块约定

- 新增服务模块时遵循现有模式：`libs/<service>/Makefile`、`module.mk`、
  `include/` 和 `src/`。
- 服务模块构建规则应复用 `libs/service_rules.mk`，不要复制一套新的通用规则。
- 暂时不要求新增、修改或运行测试模块。
- 不随意修改 public API、配置 JSON schema 或 HTTP API 契约，除非任务明确要求。
- 配置文件中的字段语义应保持向后兼容；确需变更时，要同步更新调用方和文档。

## 前端约定

- `www/` 使用 React + TypeScript + plain CSS/CSS variables。
- UI 保持 IPC/NVR 管理台风格：密集表单、紧凑左侧导航、清晰状态和深色预览区域。
- 不做营销页式布局，不添加与设备管理无关的装饰性大图或介绍页。
- 后端 API 契约参考 `www/README.md`，前端不直接拥有设备 SDK 配置解析逻辑。
- 当后端不可用时，保留现有 mock 数据路径以便 UI 开发。

## 变更原则

- 优先保持现有目录结构、命名和构建方式。
- 改动应聚焦当前任务，避免无关重构、格式化或构建产物变更。
- 不删除或重命名现有空 `.codex` 文件，除非用户明确要求。
- 涉及运行时路径时，注意当前代码使用相对路径，例如 `configs/*.json` 和
  `build/runtime/operation.log`。
