# 技术架构

## 系统概览

EniBox 将 Windows EXE 及其依赖文件打包为单文件可执行程序，运行时通过虚拟文件系统 (VFS) 透明访问。

```
┌─────────────────────────────────────────────────┐
│                  打包阶段                         │
│                                                   │
│  源EXE + 依赖文件 ──→ VFS构建 ──→ LZMA压缩       │
│                           │                       │
│                    PE修改 (添加.enibox节区)        │
│                           │                       │
│                    合并导入表 + 嵌入Loader         │
│                           ▼                       │
│                    单文件输出EXE                   │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│                  运行阶段                         │
│                                                   │
│  输出EXE启动 ──→ Loader提取 ──→ 安装API Hook     │
│                                      │            │
│                     文件访问 ──→ VFS重定向         │
│                     注册表访问 ──→ 虚拟注册表      │
│                     创建子进程 ──→ 自动注入Loader  │
└─────────────────────────────────────────────────┘
```

## 模块架构

### EniBox.GUI (C# WPF)

主应用程序，包含 GUI 界面和 CLI 入口。

```
Program.cs ──→ App.xaml.cs ──┬── GUI模式 ──→ MainWindow + MainViewModel
                              └── CLI模式 ──→ CliRunner
```

核心服务层：

| 服务 | 接口 | 职责 |
|------|------|------|
| PackService | IPackService | 封包主流程编排 |
| LzmaCompressor | ICompressor | LZMA 压缩/解压 |
| VfsBuilder | IVfsBuilder | VFS 镜像构建 |

数据模型：

| 模型 | 职责 |
|------|------|
| PackConfiguration | 封包配置（源路径、输出路径、选项） |
| PackResult | 封包结果（成功/失败、输出大小） |
| VfsHeader / VfsDirEntry / VfsFileEntry | VFS 二进制格式结构 |
| PeInfo / PeArchitecture | PE 文件信息 |
| PackErrorCode | 统一错误码常量 |

### EniBox.PeTool (C/C++ DLL)

PE 文件修改工具，通过 PInvoke 从 C# 调用。

| 函数 | 职责 |
|------|------|
| PE_Open | 打开 PE 文件，返回上下文 |
| PE_GetInfo | 获取 PE 信息（架构、节区数等） |
| PE_AddSection | 添加新节区（.enibox） |
| PE_SetEntryPoint | 设置新入口点 |
| PE_MergeImports | 合并导入表 |
| PE_ProcessTLS | 处理 TLS 回调 |
| PE_Save | 保存修改后的 PE |

### EniBox.Loader (C/C++ DLL)

VFS 运行时 Loader，嵌入到输出 EXE 的 .enibox 节区中。

| Hook 类别 | Hook 函数 | 职责 |
|-----------|----------|------|
| 文件 API | CreateFileW/NtCreateFile | 拦截文件打开，重定向到 VFS |
| 文件 API | ReadFile/NtReadFile | 从 VFS 读取数据 |
| 文件 API | CloseHandle | 关闭 VFS 句柄 |
| 文件 API | SetFilePointerEx | VFS 文件指针定位 |
| 文件 API | WriteFile | 拦截写入操作 |
| 注册表 API | RegOpenKeyExA/W | 虚拟注册表键打开 |
| 注册表 API | RegQueryValueExA/W | 虚拟注册表值查询 |
| 注册表 API | RegCloseKey/RegEnumValue | 虚拟注册表关闭/枚举 |
| 进程 API | CreateProcessA/W | 子进程创建 + Loader 注入 |

VFS 运行时组件：

| 组件 | 职责 |
|------|------|
| vfs_runtime | VFS 初始化、文件查找、数据读取 |
| vfs_hashtable | VFS 路径哈希表 |
| hook_fileapi | 文件 API Hook 安装 |
| hook_filemapping | 文件映射 Hook |
| hook_registry | 注册表 Hook 安装 |
| hook_process | 进程创建 Hook + DLL 注入 |
| inject | 远程 DLL 注入 (三级策略: QueueUserAPC → NtCreateThreadEx → CreateRemoteThread) |

## 数据流

### 打包数据流

```
源EXE ──→ PeTool.Open ──→ PeTool.GetInfo ──→ 架构判断
                                                    │
依赖文件 ──→ VfsBuilder.AddFile ──→ VfsBuilder.Build ──→ VFS镜像
                                                    │
VFS镜像 ──→ LzmaCompressor.Compress ──→ 压缩数据    │
                                                    │
Loader DLL + 压缩数据 ──→ CombineSectionData        │
                                                    │
PeTool.AddSection(.enibox) ──→ PeTool.MergeImports  │
PeTool.ProcessTLS ──→ PeTool.SetEntryPoint          │
PeTool.Save ──→ 输出EXE
```

### 运行时数据流

```
输出EXE启动
  │
  ▼
Loader DllMain (DLL_PROCESS_ATTACH)
  ├── 从.enibox节区提取VFS数据和Loader DLL
  ├── 写入Loader DLL到临时目录 (%TEMP%\EniBox-<pid>-<rnd>/)
  ├── CRC32写后回读完整性校验
  ├── LoadLibrary加载Loader DLL
  │
  ▼
Loader初始化
  ├── VReg_Initialize (虚拟注册表)
  ├── HookFile_Install (文件API Hook)
  ├── HookRegistry_Install (注册表API Hook)
  ├── HookProcess_Install (进程API Hook)
  │
  ▼
目标程序运行
  ├── CreateFileW("data.bin") ──→ VFS查找 ──→ 返回VFS句柄
  ├── ReadFile(VFS句柄) ──→ VFS数据读取
  ├── RegOpenKeyEx ──→ 虚拟注册表查找
  └── CreateProcess ──→ 挂起创建 ──→ 三级注入(APC/NtCreateThreadEx/CreateRemoteThread) ──→ 恢复执行
```

## 错误码体系

| 范围 | 类别 | 示例 |
|------|------|------|
| 1000-1999 | 文件 I/O | FileNotFound=1001 |
| 2000-2999 | PE 格式 | InvalidPe=2001, UnsupportedArch=2002 |
| 3000-3999 | VFS | VfsBuildFailed=3001 |
| 4000-4999 | 压缩 | CompressionFailed=4001 |
| 5000-5999 | PE 修改 | WriteFailed=5003, ImportMergeFailed=5002 |
| 6000-6999 | Loader | LoaderNotFound=6001, ApcFail=6607, NtCreateFail=6608 |
| 6800-6899 | 提取 | Security=6801, Integrity=6802, WriteFail=6803 |
| 9000-9999 | 通用 | OperationCancelled=9001, UnexpectedError=9999 |

## 二进制格式

### .enibox 节区布局

```
┌──────────────────────┐
│ 入口点跳转存根        │  ← 新入口点执行此存根后跳转到原始入口
├──────────────────────┤
│ Loader DLL (x64)     │  ← 嵌入的 Loader DLL 二进制数据
├──────────────────────┤
│ VFS Header           │  ← VfsHeader 结构 (44 bytes)
├──────────────────────┤
│ VFS Metadata         │  ← 目录项 + 文件项
├──────────────────────┤
│ VFS Data Region      │  ← 文件内容 (可选压缩)
└──────────────────────┘
```

### VfsHeader 结构 (44 bytes)

| 字段 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| Magic | 0 | 4 | 0x42494E45 ('ENIB') |
| Version | 4 | 4 | 格式版本 (当前=1) |
| FileCount | 8 | 4 | 文件数量 |
| DirCount | 12 | 4 | 目录数量 |
| MetadataOffset | 16 | 4 | 元数据偏移 |
| MetadataSize | 20 | 4 | 元数据大小 |
| DataOffset | 24 | 4 | 数据区偏移 |
| DataSize | 28 | 4 | 数据区大小 |
| LoaderOffset | 32 | 4 | Loader 偏移 |
| LoaderSize | 36 | 4 | Loader 大小 |
| Checksum | 40 | 4 | CRC32 校验和 |
