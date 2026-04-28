# Changelog

All notable changes to EniBox will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
