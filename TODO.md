# EniBox 待开展内容

## 已完成项 ✅

### 原始 TODO (#1-#10) — v0.1.0 - v0.5.0

| # | 内容 | 版本 |
|---|------|------|
| 1 | PE_ModMergeImports 空导入表处理 | 0.2.0 |
| 2 | PE_ModProcessTLS 回调数组修补 | 0.2.0 |
| 3 | 端到端真实 PE 打包测试 | 0.2.0 |
| 4 | Loader DLL 嵌入到输出 EXE | 0.2.0 |
| 5 | VFS 句柄伪文件系统完善 | 0.3.0 |
| 6 | MinHook VEX/AVX 指令支持 | 0.1.0 |
| 7 | 错误码与 C 端对齐验证 | 0.2.0 |
| 8 | CLI 模式 System.CommandLine 版本 | 0.1.0 |
| 9 | 发布配置优化 | 0.1.0 |
| 10 | 子进程注入架构匹配 | 0.2.0 |

### P0 安全加固 — v0.5.0

| 改进 | 状态 |
|------|------|
| P0-02 DLL 提取安全加固 (随机子目录隔离 + CRC32 + WRITE_THROUGH) | ✅ 已完成 |
| P0-03 三级注入策略 (APC → NtCreateThreadEx → CreateRemoteThread) | ✅ 已完成 |
| P0-02/P0-03 MSVC 编译验证 (Release\|x64, 0 错误 0 警告) | ✅ 已完成 |

### Bug 修复 — v0.5.0

| 改进 | 状态 |
|------|------|
| VfsBuilder 根节点 KeyNotFoundException (TryGetValue 替代直接索引) | ✅ 已完成 |
| loader_errors.h 缺失 VFS_ERR_NO_MEMORY 6009 | ✅ 已完成 |
| lzma_dec.c 重复/格式错误注释行 | ✅ 已完成 |
| ProgressCallback 测试 (SynchronousProgress\<T\>) | ✅ 已完成 |

### P1 性能优化 — v0.5.0

| 改进 | 状态 |
|------|------|
| VfsBuilder 流式处理 (大文件 >64KB 用 FileStream+CompressStream) | ✅ 已完成 |
| LzmaCompressor 零拷贝 (CompressArray/DecompressArray 重载) | ✅ 已完成 |
| ICompressor 扩展默认接口方法 (CompressStream/CompressArray/DecompressArray) | ✅ 已完成 |
| Crc32 分块计算 (ContinueCompute/StartPartial/FinishPartial) | ✅ 已完成 |

### P2 代码质量 — v0.5.0

| 改进 | 状态 |
|------|------|
| 空 catch 块补充 Debug.WriteLine 日志 | ✅ 已完成 |
| 伪异步改同步 (新增 Pack() 方法, PackAsync 改为 Task.FromResult) | ✅ 已完成 |
| 硬编码字符串常量化 (PackConstants/PeConstants/MessageConstants) | ✅ 已完成 |
| DI 容器替代 new (Microsoft.Extensions.DependencyInjection 8.0.1) | ✅ 已完成 |

### 文档与工程 — v0.5.0

| 改进 | 状态 |
|------|------|
| README/CHANGELOG/ARCHITECTURE/CONTRIBUTING/TODO 同步至 v0.5.0 | ✅ 已完成 |
| .editorconfig 代码风格配置 | ✅ 已完成 |

### P0 稳定性加固 — v0.5.1

| 改进 | 状态 |
|------|------|
| PE 输入验证防止 native 崩溃 (SizeOfOptionalHeader==0 / NumberOfSections==0) | ✅ 已完成 |
| C# 侧 PE 头预验证（防御纵深，不依赖 C++ 重新编译） | ✅ 已完成 |
| PeBoundaryTests: 8 个 PE 畸形输入测试 | ✅ 已完成 |

### P1 工程化 — v0.5.1

| 改进 | 状态 |
|------|------|
| CI/CD 流水线 (GitHub Actions) | ✅ 已完成 |
| LICENSE 文件 (MIT) | ✅ 已完成 |
| C/C++ 编译警告清理 (C4146, C4244) | ✅ 已完成 |
| VfsDirNode 重写 GetHashCode/Equals | ✅ 已完成 |
| E2E 并行竞争修复 (TempFileHelper 测试名前缀) | ✅ 已完成 |

---

## 待完善项

### P3 低优先级

| # | 内容 | 说明 |
|---|------|------|
| 1 | ICompressor 零拷贝限制 | ReadOnlySpan 不能隐式转 ReadOnlyMemory，MemoryMarshal.TryGetArray 优化受限 |
| 2 | 缓冲区大小审计 | C/C++ 侧固定缓冲区溢出风险审查 |
| 3 | x86 架构编译验证 | 仅验证了 x64，Win32\|Release 未编译 |
