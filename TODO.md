# EniBox 待开展内容

## 🔴 关键缺陷修复

### ~~1. PE_ModMergeImports 空导入表处理~~ ✅ 已修复
- **修复内容**: 当源 EXE 没有导入表时，在 `.enibox` 节中创建完整的导入目录结构（IMAGE_IMPORT_DESCRIPTOR + ILT + IAT + DLL name string），并更新 DataDirectory 指向新导入表

### ~~2. PE_ModProcessTLS 回调数组修补不完整~~ ✅ 已修复
- **修复内容**: 完善了 TLS 回调处理逻辑，正确保存原始 TLS 回调 VA 到 `.enibox` 节。由于 Loader 通过导入表在 TLS 之前加载（DLL_PROCESS_ATTACH 先于 TLS callbacks），回调数组无需修补。添加了安全回退机制和详细注释

## 🟡 功能增强

### ~~3. 端到端真实 PE 打包测试~~ ✅ 已完成
- **完成内容**: 添加 `RealPeToolE2ETests`（5 个测试）使用 fc.exe 作为源，验证完整 Open→AddSection→ProcessTLS→MergeImports→Save 流程，输出 EXE 包含 `.enibox` 节且 PE 结构有效。同时修复了 `MergeImports` 的 GCHandle pinning 问题（改用 `Marshal.AllocHGlobal` + `StructureToPtr`）

### 4. Loader DLL 嵌入到输出 EXE
- **问题**: 当前 `CombineSectionData` 将 Loader DLL 字节写入 `.enibox` 节数据区，但 Loader 的 `DllMain` 是通过导入表触发的——导入表引用的是外部 `EniBox.Loader.dll` 文件，而非节内嵌入的副本
- **需要**: 两种方案选一：(A) 运行时从 `.enibox` 节提取 Loader DLL 写入临时文件再 LoadLibrary，或 (B) 修改导入表合并逻辑使 Loader 通过入口点存根加载而非导入表

### 5. VFS 句柄伪文件系统完善
- **问题**: `VFS_HandleAlloc` 返回的伪句柄需要确保不与真实 OS 句柄冲突，且 `Hook_ReadFile`/`Hook_GetFileSize` 等需要正确路由伪句柄到 VFS 读取
- **需要**: 验证伪句柄范围（使用高地址或特殊标志位），确保所有 FileAPI Hook 正确处理伪句柄与真实句柄的分流

## 🟢 质量与体验

### 6. MinHook VEX/AVX 指令支持
- **问题**: 增强版 `GetInstructionLength` 仍不支持 VEX 前缀（3 字节 `C4`/`C5`）和 EVEX 前缀（4 字节 `62`），AVX/AVX2 函数的 Hook 可能失败
- **需要**: 添加 VEX/EVEX 前缀解码，或集成完整 MinHook 库

### ~~7. 错误码与 C 端对齐验证~~ ✅ 已完成
- **完成内容**: 添加 `ErrorCodeAlignmentTests`（2 个测试）验证 PackErrorCode 常量与 PeTool C 端 `PE_ERR_*` 数值一致，并通过 PeTool DLL 实际调用验证错误码返回值。修正了 `WriteFailed`（1003→5003）和 `ReadFailed`（1002→5004）与 C 端对齐

### 8. CLI 模式 System.CommandLine 版本
- **问题**: 使用的是 `2.0.0-beta4` 预发布版，API 可能在未来版本中变化
- **需要**: 锁定版本或迁移到稳定版 `System.CommandLine`

### 9. 发布配置优化
- **问题**: 未配置 `PublishSingleFile`、`PublishTrimmed`、`IncludeNativeLibrariesForSelfExtract` 等发布属性
- **需要**: 添加发布配置，使输出为单个 EXE（内嵌 Loader/PeTool DLL）

### 10. 子进程注入架构匹配
- **问题**: `Inject_ArchitectureMatches` 只检查当前进程与目标进程的位数是否匹配，未处理 WoW64 场景（32 位进程在 64 位系统上）
- **需要**: 使用 `IsWow64Process` 检测目标进程的真实架构
