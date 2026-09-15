# EniBox

[English](README.md) | 简体中文

**虚拟文件盒封包工具** — 将 Windows EXE 及其依赖文件打包为单文件可执行程序，运行时通过虚拟文件系统 (VFS) 透明访问。

[![CI](https://github.com/mikasaw/EniBox/actions/workflows/ci.yml/badge.svg)](https://github.com/mikasaw/EniBox/actions/workflows/ci.yml)
![.NET 8.0](https://img.shields.io/badge/.NET-8.0-blue)
![Windows x64](https://img.shields.io/badge/Windows-x64-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Tests](https://img.shields.io/badge/Tests-176_✔️-brightgreen)

## 特性

- **单文件输出** — 将 EXE + 依赖 DLL + 数据文件打包为一个 .enibox 可执行文件
- **虚拟文件系统 (VFS)** — 运行时通过 Win32 文件 API Hook 透明重定向文件访问，无需解压到磁盘
- **LZMA 压缩** — 使用 LZMA 算法压缩 VFS 数据，减小输出文件体积
- **注册表虚拟化（实验性）** — 封包时可预置注册表键值（`--registry-virtualization` + 编程 API），被封装程序内读取命中虚拟注册表、写入仅进程内隔离（不持久化、不污染真实注册表）；未预置的键完全透传
- **子进程 VFS 继承（实验性）** — 封包程序启动的非封包子进程会被自动注入 Loader，并通过 VfsLink 握手继承父程序的 VFS 视图；封包子进程则使用自己的 Loader（互不干扰）
- **安全加固** — DLL 提取使用随机子目录隔离 + CRC32 完整性校验 + FILE_FLAG_WRITE_THROUGH 独占写入
- **CLI + GUI 双模式** — 支持图形界面和命令行两种操作方式
- **国际化** — 支持中文 (zh-CN) 和英文 (en-US) 界面

## 快速开始

### 前置条件

- Windows 10/11 (x64)
- .NET 8.0 SDK
- 含 **MSVC v145 工具集**的 Visual Studio（本仓库在 VS 18 Insiders 上验证；其他 VS 版本需自行调整 vcxproj 中的 PlatformToolset）+ Windows 10 SDK (10.0.26100.0)

### 构建

```bash
# 构建 C/C++ 原生 DLL (PeTool + Loader)
msbuild src/EniBox.PeTool/EniBox.PeTool.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild src/EniBox.Loader/EniBox.Loader.vcxproj /p:Configuration=Release /p:Platform=x64

# 构建 .NET 项目
dotnet build src/EniBox.GUI/EniBox.GUI.csproj -c Release
```

### 发布

```bash
dotnet publish src/EniBox.GUI/EniBox.GUI.csproj -c Release -r win-x64
```

输出单文件自包含可执行程序至 `publish/` 目录。

### 运行测试

```bash
# 全量测试
dotnet test tests/EniBox.Tests/EniBox.Tests.csproj -c Release

# 按类别筛选
dotnet test --filter "FullyQualifiedName~PeBoundaryTests"
dotnet test --filter "FullyQualifiedName~PackedVfsRuntimeTests"
```

## 使用方法

### GUI 模式

直接运行 EniBox.exe，在界面中选择源 EXE 文件和输出路径，添加依赖文件后点击打包。

### CLI 模式

```bash
EniBox.exe --cli --source <源EXE路径> --output <输出路径> [选项]
```

详细 CLI 参数说明请参阅 [CLI.md](CLI.md)。

## 项目结构

```
EniBox/
├── .github/workflows/      # CI/CD 流水线 (GitHub Actions)
├── src/
│   ├── EniBox.GUI/          # WPF 应用 + CLI 入口
│   │   ├── Program.cs       # 程序入口点 (GUI/CLI 双模式)
│   │   ├── Services/        # 封包服务、CLI 运行器、压缩器
│   │   ├── Models/          # 数据模型 (VFS/PE/配置/错误码)
│   │   ├── Interop/         # PeTool DLL P/Invoke 互操作
│   │   ├── ViewModels/      # MVVM 视图模型
│   │   └── Views/           # WPF 视图
│   ├── EniBox.PeTool/       # C/C++ PE 文件修改 DLL
│   │   └── src/             # 导入表合并、TLS 处理、Section 添加
│   └── EniBox.Loader/       # C/C++ VFS 运行时 Loader DLL
│       ├── src/             # 文件/注册表/进程 Hook、VFS 运行时、LZMA 解压
│       ├── include/         # 头文件定义
│       └── deps/MinHook/    # vendored MinHook (API Hook 框架，含 HDE 反汇编器)
└── tests/
    ├── EniBox.Tests/        # xUnit 测试项目 (176 用例)
    │   ├── E2E/             # 端到端测试 (封包+运行+VFS+注册表+子进程)
    │   ├── PackService/     # 封包服务验证 (Mock+异常+输入)
    │   ├── Unit/            # 模型/VFS/压缩/互操作 单元测试
    │   ├── Boundary/        # PE 畸形输入边界测试
    │   └── TestInfrastructure/  # 测试基础设施 (ProcessRunner, TempFileHelper 等)
    └── TestHelpers/         # 测试辅助程序
        ├── VfsTest/          # C 原生 VFS 自检 (9 项运行时子测试)
        ├── FileChecker/      # C# 文件读取验证
        ├── RegChecker/       # 注册表虚拟化验证
        └── SubProcHost/      # 子进程宿主/子进程对
```

## 技术架构

详细架构说明请参阅 [ARCHITECTURE.md](ARCHITECTURE.md)。

### 封包流程

1. **解析源 PE** — 通过 PeTool DLL 读取源 EXE 的 PE 结构（架构、节区、导入表）
2. **构建 VFS** — 将依赖文件组织为虚拟文件系统镜像（目录树 + 文件数据）
3. **压缩数据** — 使用 LZMA 压缩 VFS 元数据和数据区
4. **修改 PE** — 添加 .enibox 节区、写入入口点 bootstrap、旁置 Loader DLL
5. **写入输出** — 生成包含 Loader 与 VFS 数据的单文件 EXE

### 运行时流程

1. **Loader 初始化** — 入口点 bootstrap 旁置加载 Loader DLL，解析 .enibox 节区的 VFS 数据（CRC32 校验）
2. **安装 Hook** — Hook Win32 文件 API（CreateFileA/W、ReadFile、GetFileSize、SetFilePointer、GetFileAttributes 等）
3. **透明重定向** — 目标程序的文件访问被 VFS Hook 拦截，匹配到 VFS 条目则返回内存数据，否则透传真实文件系统
4. **子进程 VFS 继承（实验性）** — 启动非封包子进程时注入 Loader 并通过 VfsLink 传递父 VFS；封包子进程与系统程序（cmd.exe 等）不在注入范围（见已知限制）

## 已知限制

发布前请了解当前版本的边界：

- **仅支持 x64** — 32 位 PE 会在封包阶段被明确拒绝
- **VFS 只读** — 对 VFS 内文件的写入会被拒绝；若目标程序运行时要写"自身目录"（配置/日志），请通过参数将其重定向到可写目录
- **子进程继承的边界** — 系统程序（cmd.exe、conhost.exe 及 System32 下的程序）不注入、不继承 VFS；注入的子进程持有父 VFS 的只读副本（父进程退出后仍可用）；"子进程再孙进程"的多级继承暂不支持
- **注册表虚拟化为实验性** — 预置值读取与写进程内隔离有 E2E 覆盖；写后持久化尚不支持
- **兼容性验证范围有限** — 目前主要在 Windows 11 (26200, Insider) x64 上完成回归；Windows 10 为目标平台但覆盖有限

## 测试覆盖 (176 用例)

| 类别 | 数量 | 覆盖内容 |
|------|:----:|---------|
| 单元测试 | ~80 | VFS 结构体序列化、CRC32、模型/配置/错误码、存根机器码 |
| 集成测试 | ~30 | VFS 构建、LZMA 压缩往返、PeTool P/Invoke、CLI 参数 |
| E2E 测试 | 27 | 封包流程、运行时运行、VFS 文件读取、Loader 提取、特殊路径 |
| 边界测试 | 8 | PE 畸形输入（截断/损坏/空/随机/SizeOfOptionalHeader=0）|
| 代码审查 | ~20 | 源文件存在性、函数签名、架构检测逻辑 |

**关键 E2E 场景**：封包程序 + 嵌入文件 → 运行 → 通过 VFS Hook **读取仅存在于内存中的数据**（磁盘上无此文件）。

## 第三方组件

本项目包含 vendored MinHook（BSD）与 HDE（BSD），并引用 LZMA-SDK 等 NuGet 包——许可声明详见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。

## 版本变更

请参阅 [CHANGELOG.md](CHANGELOG.md)。

## 贡献

请参阅 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可证

[MIT](LICENSE) — 详见 LICENSE 文件；第三方组件适用其各自许可（见上方声明）。
