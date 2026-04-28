# 贡献指南

感谢您对 EniBox 项目的关注！

## 开发环境

### 必需工具

- **Windows 10/11** (x64)
- **.NET 8.0 SDK** — [下载](https://dotnet.microsoft.com/download/dotnet/8.0)
- **Visual Studio 2022** — 含以下工作负载：
  - .NET 桌面开发
  - C++ 桌面开发 (用于 PeTool/Loader)
- **Git** — 版本控制

### 可选工具

- **MinHook** — 已作为子模块包含，用于 API Hook
- **LZMA SDK** — 通过 NuGet 包引用

## 项目构建

### 1. 构建 C/C++ 原生 DLL

在 Visual Studio 中打开并构建：

- `src/EniBox.PeTool/EniBox.PeTool.vcxproj` (Release, x64)
- `src/EniBox.Loader/EniBox.Loader.vcxproj` (Release, x64)

确保输出 DLL 在 `x64/Release/` 目录下。

### 2. 构建 .NET 项目

```bash
dotnet build src/EniBox.GUI/EniBox.GUI.csproj -c Debug
```

### 3. 运行测试

```bash
dotnet test tests/EniBox.Tests/EniBox.Tests.csproj
```

## 代码规范

### C# 代码

- 遵循 [C# 编码约定](https://learn.microsoft.com/zh-cn/dotnet/csharp/fundamentals/coding-style/coding-conventions)
- 使用 `nullable enable`
- XML 文档注释用于公共 API
- 命名空间与目录结构一致

### C/C++ 代码

- 遵循项目现有风格（K&R 大括号、4 空格缩进）
- 函数命名：模块前缀 + 下划线 + 功能名（如 `VReg_Initialize`）
- 头文件使用 `#pragma once` 或传统 include guard
- 错误码使用负值返回，0 表示成功

## 测试规范

### 测试结构

```
tests/EniBox.Tests/
├── TestInfrastructure/    # 测试基础设施 (E2ETestBase, ProcessRunner, TestExeBuilder, SynchronousProgress 等)
├── Config/                # 配置模型测试
├── Vfs/                   # VFS 数据结构测试
├── PackService/           # 封包服务测试
├── Interop/               # 互操作测试
├── Compression/           # 压缩测试
├── Cli/                   # CLI 参数测试
├── Registry/              # 注册表验证测试
├── Injection/             # 注入验证测试
├── Gui/                   # GUI 验证测试
├── E2E/                   # 端到端测试 (封包+运行+VFS+注册表+子进程+特殊路径)
└── Integration/           # 集成测试
tests/TestHelpers/         # C# 测试辅助程序
├── FileChecker/           # Win32 文件读取验证
├── RegChecker/            # 注册表虚拟化验证
├── SubProcHost/           # 子进程宿主
└── SubProcChild/          # 子进程 VFS 验证
```

### 测试约定

- 使用 xUnit 框架 + Moq 隔离
- 测试方法命名：`方法名_场景_预期结果`
- 依赖原生 DLL 的测试使用 `PeToolAvailabilityChecker.IsAvailable` 条件跳过
- 临时文件使用 `TempFileHelper` 自动清理

### 运行特定测试

```bash
# 按类别过滤
dotnet test --filter "FullyQualifiedName~PackServiceTests"

# 按优先级（测试特性标记）
dotnet test --filter "Trait=Priority0"
```

## 提交规范

### 提交信息格式

```
<type>: <description>

[可选的详细说明]
```

类型：

| 类型 | 说明 |
|------|------|
| feat | 新功能 |
| fix | 修复缺陷 |
| test | 测试用例 |
| docs | 文档 |
| refactor | 重构 |
| chore | 构建/工具变更 |

### 示例

```
feat: add registry virtualization support for RegEnumValue API

fix: resolve empty import table crash in MergeImports

test: add PackService input validation tests
```

## 发布流程

1. 更新 `CHANGELOG.md`
2. 构建 Release 配置
3. 运行全量测试
4. 发布单文件可执行程序
