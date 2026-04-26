# EniBox 待开展内容

## 🔴 关键缺陷修复

### ~~1. PE_ModMergeImports 空导入表处理~~ ✅ 已修复
- **修复内容**: 当源 EXE 没有导入表时，在 `.enibox` 节中创建完整的导入目录结构（IMAGE_IMPORT_DESCRIPTOR + ILT + IAT + DLL name string），并更新 DataDirectory 指向新导入表

### ~~2. PE_ModProcessTLS 回调数组修补不完整~~ ✅ 已修复
- **修复内容**: 完善了 TLS 回调处理逻辑，正确保存原始 TLS 回调 VA 到 `.enibox` 节。由于 Loader 通过导入表在 TLS 之前加载（DLL_PROCESS_ATTACH 先于 TLS callbacks），回调数组无需修补。添加了安全回退机制和详细注释

## 🟡 功能增强

### ~~3. 端到端真实 PE 打包测试~~ ✅ 已完成
- **完成内容**: 添加 `RealPeToolE2ETests`（5 个测试）使用 fc.exe 作为源，验证完整 Open→AddSection→ProcessTLS→MergeImports→Save 流程，输出 EXE 包含 `.enibox` 节且 PE 结构有效。同时修复了 `MergeImports` 的 GCHandle pinning 问题（改用 `Marshal.AllocHGlobal` + `StructureToPtr`）

### ~~4. Loader DLL 嵌入到输出 EXE~~ ✅ 已完成
- **完成内容**: 实现方案 A——运行时从 `.enibox` 节提取 Loader DLL 到临时文件。新增 `vfs_total_size:4` 字段到节布局，Loader DllMain 据此正确分割 VFS 数据和嵌入的 Loader DLL 字节。`ExtractEmbeddedLoader` 将 DLL 写入 `%TEMP%\EniBox.Loader.<pid>.dll`，供子进程注入使用。`CleanupExtractedLoader` 在 DLL_PROCESS_DETACH 时清理临时文件（含 MOVEFILE_DELAY_UNTIL_REBOOT 兜底）。修复了之前 VFS 初始化误将 Loader DLL 字节当作 VFS 数据的 bug

### ~~5. VFS 句柄伪文件系统完善~~ ✅ 已完成
- **完成内容**: 添加 3 个关键 FileAPI Hook：(1) `Hook_CloseHandle` — 释放伪句柄资源，防止内存泄漏；(2) `Hook_SetFilePointer` — 路由文件 seek 操作到 VFS，支持 FILE_BEGIN/CURRENT/END；(3) `Hook_WriteFile` — 拒绝对只读 VFS 的写入（ERROR_ACCESS_DENIED）。伪句柄范围 `0xFFFF0000..0xFFFF0FFF` 已确保不与真实 OS 句柄冲突

## 🟢 质量与体验

### ~~6. MinHook VEX/AVX 指令支持~~ ✅ 已完成
- **完成内容**: 在 `GetInstructionLength` 中添加 VEX 2字节前缀 (C5)、VEX 3字节前缀 (C4) 和 EVEX 4字节前缀 (62) 解码。VEX.C5 映射到 0F map1，VEX.C4 支持 0F/0F38/0F3A/XOP maps，EVEX 支持 AVX-512。正确处理 ModRM + imm8 操作数（如 VPSHUFD、0F3A map 指令）。仅在 x64 模式下启用（x86 下这些字节码含义不同）

### ~~7. 错误码与 C 端对齐验证~~ ✅ 已完成
- **完成内容**: 添加 `ErrorCodeAlignmentTests`（2 个测试）验证 PackErrorCode 常量与 PeTool C 端 `PE_ERR_*` 数值一致，并通过 PeTool DLL 实际调用验证错误码返回值。修正了 `WriteFailed`（1003→5003）和 `ReadFailed`（1002→5004）与 C 端对齐

### ~~8. CLI 模式 System.CommandLine 版本~~ ✅ 已完成
- **完成内容**: 保持 `2.0.0-beta4` 版本（稳定版 2.0.x API 完全不兼容，无 SetHandler/AddOption/Invoke 等方法）。版本已锁定，不再自动升级

### ~~9. 发布配置优化~~ ✅ 已完成
- **完成内容**: 添加 Release 配置：`PublishSingleFile=true`、`SelfContained=true`、`IncludeNativeLibrariesForSelfExtract=true`、`EnableCompressionInSingleFile=true`。GUI 模式禁用 trimming（WPF 不兼容），CLI 模式可启用

### ~~10. 子进程注入架构匹配~~ ✅ 已完成
- **完成内容**: 重写 `Inject_ArchitectureMatches`，优先使用 `IsWow64Process2`（Windows 10+）精确检测目标进程架构，回退到 `IsWow64Process`（Windows XP+），最终回退到简单位数比较。正确处理 WoW64 场景：32 位进程在 64 位系统上运行时，`processMachine` 返回 `IMAGE_FILE_MACHINE_I386`
