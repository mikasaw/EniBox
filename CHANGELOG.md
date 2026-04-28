# Changelog

All notable changes to EniBox will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [0.5.0] - 2026-04-28

### Added
- 端到端封包测试 (FullPackFlowTests): fc.exe 完整封包 + 含依赖封包
- 端到端运行测试 (PackedExeRunTests): 封包 EXE 启动并正常退出
- 端到端 VFS 文件访问透传测试 (VfsRuntimeAccessTests)
- 端到端 Loader DLL 提取测试 (LoaderExtractionTests)
- 端到端特殊路径测试: 空格路径 + 中文路径 (SpecialPathTests)
- 端到端重复封包测试 (RepackTests)
- 端到端多文件压力测试: 10 个依赖文件封包 (MultiFileStressTests)
- 运行时 E2E 测试: FileChecker (Win32 文件读取) + RegChecker (注册表虚拟化) + SubProcHost/Child (子进程)
- C# 测试辅助程序: FileChecker, RegChecker, SubProcHost, SubProcChild
- PeTool 完整流水线 E2E 测试 (RealPeToolE2ETests): AddSection/GetInfo/Open/TLS/ImportMerge/Save
- P0-02 DLL 提取安全加固: 随机子目录隔离、FILE_FLAG_WRITE_THROUGH、CRC32 写后回读校验
- P0-03 三级注入策略: QueueUserAPC → NtCreateThreadEx → CreateRemoteThread 自动回退
- 新增错误码: INJECT_ERR_APC_FAIL(6607), INJECT_ERR_NTCREATE_FAIL(6608), EXTRACT_ERR_SECURITY(6801), EXTRACT_ERR_INTEGRITY(6802), EXTRACT_ERR_WRITE_FAIL(6803)
- SynchronousProgress<T> 测试基础设施: 解决 xunit 无 SynchronizationContext 下 Progress<T> 异步投递问题
- E2ETestBase, ProcessRunner, TestExeBuilder, TempFileHelper 测试基础设施
- 高级功能完整性验证测试 (15 个代码审查用例)

### Changed
- VfsBuilder O(n^2) 目录索引查找优化为 Dictionary O(1)
- VfsBuilder 根节点 ParentIndex 处理: TryGetValue 替代直接索引，避免 KeyNotFoundException
- Inject_LoadDll 内部调用 Inject_DetectBestMethod + Inject_LoadDllEx，子进程注入自动使用三级策略
- loader_main.c 添加 #include <strsafe.h> (StringCchCopyW 依赖)

### Fixed
- VfsBuilder 根节点 (Name=="") 不在 dirIndexMap 中导致 KeyNotFoundException
- Progress<T> 在 xunit 测试中异步投递导致 ProgressCallback 断言失败

## [0.4.0] - 2026-04-28

### Added
- Program.cs 主入口点，支持 GUI/CLI 双模式启动
- StartupObject 配置指定入口类型
- 高级功能完整性验证测试 (15 个代码审查用例)
- 配置模型测试：PackConfiguration、PackResult、PackErrorCode、PeArchitecture、PeInfo、PackException、PackFileItem (13 个)
- VFS 数据结构测试：VfsHeader 序列化往返、Crc32 标准校验值 (13 个)
- PackService 输入验证测试：空路径、不存在文件 (4 个)
- PackService Moq 隔离测试：非 PE 文件、取消操作 (2 个)
- PackService 异常处理测试：VfsBuilder 异常、PackException、进度回调 (3 个)
- PeToolInterop 结构封送测试：ImportEntry 大小、DllName 封送 (5 个)
- PeToolInterop PInvoke 调用测试 (2 个)
- LzmaCompressor 压缩往返测试 (5 个)
- CLI 参数解析测试：必需参数缺失、--help、可选参数 (9 个)
- Moq 测试依赖和测试基础设施 (TempFileHelper、PeToolAvailabilityChecker、VerificationTestBase)
- xunit.runner.json 配置

## [0.3.0] - 2026-04-27

### Added
- SetFilePointerEx Hook 支持
- 异步取消支持 (CancellationToken)
- VFS 线程安全改进
- PE 解析去重

### Fixed
- 移除 hook_fileapi 中重复的 CloseHandle Hook（hook_filemapping 已处理）

## [0.2.0] - 2026-04-26

### Added
- Loader DLL 嵌入 .enibox 节区并运行时自动提取
- 所有剩余 TODO 项完成 (#4-#6, #8-#10)
- 真实 PeTool E2E 测试
- 错误码对齐 (PackErrorCode 与 PeTool C 错误码统一)

### Fixed
- 空导入表处理
- TLS 回调处理
- MergeImports 调用修正 (ImportEntry 结构)

## [0.1.0] - 2026-04-25

### Added
- 核心封包功能：PE 解析 + VFS 构建 + LZMA 压缩 + PE 修改
- VFS 运行时：文件 API Hook (CreateFileW/NtCreateFile/ReadFile/WriteFile/CloseHandle)
- 注册表虚拟化：RegOpenKeyEx/RegQueryValueEx Hook
- 子进程注入：CreateProcess Hook + DLL 注入 + 架构检测
- WPF GUI 界面 + MainViewModel
- CLI 模式 (--cli 参数)
- 国际化支持 (zh-CN/en-US)
- 统一 PackErrorCode 常量
- MinHook 集成 (支持 VEX/AVX 指令)
