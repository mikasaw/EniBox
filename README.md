# EniBox

**虚拟文件盒封包工具** — 将 Windows EXE 及其依赖文件打包为单文件可执行程序，运行时通过虚拟文件系统 (VFS) 透明访问。

## 特性

- **单文件输出** — 将 EXE + 依赖 DLL + 数据文件打包为一个 .enibox 可执行文件
- **虚拟文件系统 (VFS)** — 运行时通过 API Hook 透明重定向文件访问，无需解压到磁盘
- **LZMA 压缩** — 使用 LZMA 算法压缩 VFS 数据，减小输出文件体积
- **注册表虚拟化** — 可选 Hook 注册表 API，隔离注册表读写
- **子进程注入** — 三级注入策略 (QueueUserAPC → NtCreateThreadEx → CreateRemoteThread)，自动将 Loader DLL 注入子进程，确保子进程也能访问 VFS
- **安全加固** — DLL 提取使用随机子目录隔离 + CRC32 完整性校验 + FILE_FLAG_WRITE_THROUGH 独占写入
- **CLI + GUI 双模式** — 支持图形界面和命令行两种操作方式
- **国际化** — 支持中文 (zh-CN) 和英文 (en-US) 界面

## 快速开始

### 前置条件

- Windows 10/11 (x64)
- .NET 8.0 SDK
- Visual Studio 2022 (含 C++ 桌面开发工作负载) 或 MSVC Build Tools

### 构建

```bash
# 构建 C/C++ 原生 DLL (PeTool + Loader)
# 需要先在 Visual Studio 中构建 src/EniBox.PeTool 和 src/EniBox.Loader

# 构建 .NET 项目
dotnet build src/EniBox.GUI/EniBox.GUI.csproj -c Release
```

### 发布

```bash
dotnet publish src/EniBox.GUI/EniBox.GUI.csproj -c Release -r win-x64
```

输出单文件自包含可执行程序至 `publish/` 目录。

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
├── src/
│   ├── EniBox.GUI/          # WPF 应用 + CLI 入口
│   │   ├── Program.cs       # 程序入口点
│   │   ├── App.xaml.cs      # GUI/CLI 模式判断
│   │   ├── Services/        # 封包服务、CLI 运行器、压缩器
│   │   ├── Models/          # 数据模型 (VFS/PE/配置)
│   │   ├── Interop/         # PeTool DLL PInvoke 互操作
│   │   ├── ViewModels/      # MVVM 视图模型
│   │   └── Views/           # WPF 视图
│   ├── EniBox.PeTool/       # C/C++ PE 文件修改 DLL
│   │   └── src/             # 导入表合并、TLS 处理、Section 添加
│   └── EniBox.Loader/       # C/C++ VFS 运行时 Loader DLL
│       └── src/             # 文件 Hook、注册表 Hook、进程 Hook、VFS 运行时
└── tests/
    ├── EniBox.Tests/        # xUnit 测试项目 (161 用例)
    └── TestHelpers/         # C# 测试辅助程序 (FileChecker, RegChecker, SubProcHost/Child)
```

## 技术架构

详细架构说明请参阅 [ARCHITECTURE.md](ARCHITECTURE.md)。

核心工作流程：

1. **解析源 PE** — 读取源 EXE 的 PE 结构（架构、节区、导入表）
2. **构建 VFS** — 将依赖文件组织为虚拟文件系统镜像
3. **压缩数据** — 使用 LZMA 压缩 VFS 元数据和数据区
4. **修改 PE** — 添加 .enibox 节区、合并导入表、处理 TLS 回调、设置入口点
5. **写入输出** — 生成包含嵌入 Loader + VFS 数据的单文件 EXE

运行时：

1. **Loader 初始化** — 从 .enibox 节区提取 VFS 数据和 Loader DLL（CRC32 校验 + 随机子目录隔离）
2. **安装 Hook** — Hook 文件 API (CreateFileW/NtCreateFile 等)、注册表 API、进程 API
3. **透明重定向** — 目标程序访问文件时自动重定向到 VFS
4. **子进程注入** — 创建子进程时自动注入 Loader（三级策略: APC → NtCreateThreadEx → CreateRemoteThread）

## 版本变更

请参阅 [CHANGELOG.md](CHANGELOG.md)。

## 贡献

请参阅 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可证

私有项目，未开源。
