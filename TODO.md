# EniBox 待开展内容

## 已全部完成 ✅

所有原始 TODO 项 (#1-#10) 均已在 v0.1.0 - v0.5.0 中完成：

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

## v0.5.0 新增改进

| 改进 | 状态 |
|------|------|
| P0-02 DLL 提取安全加固 (随机子目录隔离 + CRC32 + WRITE_THROUGH) | ✅ 已完成 |
| P0-03 三级注入策略 (APC → NtCreateThreadEx → CreateRemoteThread) | ✅ 已完成 |
| VfsBuilder O(n^2) → O(1) Dictionary 优化 | ✅ 已完成 |
| VfsBuilder 根节点 KeyNotFoundException 修复 | ✅ 已完成 |
| 161 项测试全部通过 (含 20 项 E2E) | ✅ 已完成 |
| 项目文档同步更新 | ✅ 已完成 |

## 未来可改进项 (P1-P3)

### P1
- CI/CD 流水线配置 (GitHub Actions / Azure Pipelines)
- LICENSE 文件
- ReadAllBytes 内存优化 (大文件流式处理)
- LzmaCompressor 零拷贝 (Span<byte>)

### P2
- 空 catch 块补充日志
- 伪异步方法改为同步
- 硬编码字符串提取为常量
- DI 容器替代 new 创建服务

### P3
- 缓区大小风险审计
- 更多 PE 格式边界条件测试
- x86 架构支持验证
