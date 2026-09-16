# Changelog

All notable changes to EniBox will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- **注册表删除语义** — `RegDeleteValue`/`RegDeleteKey`/`RegDeleteTree`/`RegDeleteKeyEx` 全部挂钩（钩子 11→19）：删除跨启动持久（存储即真相）、删键有子键时返回 ACCESS_DENIED 与真实注册表一致、`RegDeleteTree` 删子键与值键本身保留、已开句柄按 `ERROR_KEY_DELETED` 拒绝（墓碑槽位实现）、**作用域根删除被拒绝**（防止子树退出虚拟化后写入逃逸到真实注册表）
- **虚拟父句柄路径解析** — 以虚拟句柄为父句柄的 Open/Create/Delete 子键操作不再落穿透（BuildKeyPath 解析句柄指向的键路径）
- **sidecar 原子替换** — 写值落盘改为「进程唯一临时名 + MoveFileEx 原子替换」，读者侧共享 DELETE 语义：并发实例下 sidecar 始终完整，torn 写窗口归零
- **CLI 预置注册表值** — `--registry-value KEY|NAME|TYPE|DATA`（可重复；TYPE ∈ SZ|EXPAND_SZ|DWORD|BINARY，缺省 SZ），配合 `--registry-virtualization` 全 CLI 化封包；开关与预置值不匹配时双向告警

### Fixed
- **CFG 加固二进制（MSVC 系统程序）封包后间接调用崩溃** — 旧做法清除 GUARD_CF 标志导致加载器跳过 CFG 位图初始化，`_guard_dispatch_icall` 首个间接调用即跳入非法地址（certutil.exe 实证）；现保持 GUARD_CF 并把新入口点登记进 GFIDS 表（表重建于 .enibox 尾部，条目 4 字节 RVA + GuardFlags 元数据步进）
- **同键多条预置值静默丢失** — 序列化器曾把同键多值拆成独立键槽，读取只命中第一条（自注册表虚拟化引入即存在，验收代理复现）；现 C# 侧按 KeyPath 聚合、Loader 侧同路径条目合并双保险
- **注册表值钩子并发窗口** — Query/Enum 虚拟分支改锁内读，SetValue/DeleteValue 墓碑守卫改锁内复查（删除 free 与无锁读的 UAF 窗口）
- **GUI 单文件产物 `--cli` 路由失效**（v0.6.0 发布冒烟发现）— 路由开关被原样透传给参数解析器报「未知选项」，命中后剥离再解析；`--version` 现输出 `0.6.0+<commit>`

## [0.6.0] - 2026-09-16

子进程 VFS 继承、注册表虚拟化 + 持久化完整落地；修复封包产物启动崩溃全链（8 条根因）。

### Added
- **子进程 VFS 继承（实验性）** — 封包程序启动的非封包子进程自动注入 Loader 并经 VfsLink 握手继承父 VFS 视图；系统程序跳过名单（cmd.exe/conhost/System32 等）不注入；封包子进程跳过注入防同名模块遮蔽；A/W 双族 E2E 覆盖
- **注册表虚拟化（实验性）+ 持久化** — 封包时预置键值（编程 API）；作用域 = 前缀子树规则（预置键路径即作用域根，含运行时 `RegCreateKeyEx` 新建键，注册表钩子 9→11）；写入不触碰真实注册表并持久化到产物同目录 `<产物>.vreg.bin`（CRC 校验、后写赢无锁、删除即重置为预置值）；继承子进程经父镜像路径读写同一份 sidecar；E2E 覆盖 写→重启读回→删档重置→真实注册表零写入
- **封包配置开关** — .enibox 节 [272..275] 配置位（bit0 子进程注入、bit1 注册表虚拟化），CLI `--subprocess-injection` / `--registry-virtualization` 显式控制，Loader 按位门控
- **Loader 磁盘优先选取** — `Resources\EniBox.Loader.<arch>.dll` → 同目录 → 通用名，MZ/PE 架构校验，CLI 进度输出打印 SHA256 指纹前 16 位
- **CI/CD** — GitHub Actions 五 job 流水线（msvc / dotnet-build / dotnet-test / cli-mode-build / e2e-verify）全绿；`w-inject-debug` 手动诊断工作流；E2E 验证脚本 `verify-e2e.ps1` 集成
- **全局异常兜底** — GUI 三级异常钩子 + CLI 兜底 catch，写 `%LOCALAPPDATA%\EniBox\logs\` 文件日志，CLI 返回非零退出码
- E2E 用例 27→28（子进程继承、注册表预置、注册表持久化往返）；SubProcHost/DbWinSniffer 测试助手重写为原生 C（.NET 版易被杀软隔离）

### Fixed
- **封包产物启动崩溃全链（8 条根因，tests/E2E-RUNBOOK.md §5.1 存档）** — 旁置 Loader DLL 从不落地、入口点所在节不可执行（0xC0000040→0xE0000040）、VFS 头 DataOffset 恒 0、盘根路径 DirIndex 失配、陈旧嵌入 Loader（上游 MinHook 替换）、CombineSectionData 流位置错位、W 族子进程注入 detour 未接线、预定义根键句柄符号扩展失配（SDK 常量 vs 应用零扩展，致注册表虚拟化对真实应用永不命中）
- `--compress` 参数被静默忽略（bool 绑定错误），现支持 on|off|true|false|yes|no|1|0
- CLI 缺失 `--files`/`--dirs` 路径时报错退出（退出码契约 0/1/2），不再静默成功

### Changed
- **破坏性变更：VFS blob 格式 v2** — header 44→52 字节（追加 RegistryOffset/RegistrySize），尾部新增 'EREG' 注册表预置区并计入 CRC；v1 旧封包产物不被新 Loader 接受，需用新版本重新封包
- 测试总数 176→177，E2E 27→28

## [0.5.1] - 2026-06-24

### Fixed
- 修复 PeTool 处理 NumberOfSections=0 的 PE 时 native 崩溃（AccessViolationException）
- 加强 PE 输入验证：SizeOfOptionalHeader==0、NumberOfSections==0 提前拒绝
- C# 侧 PE 头预验证（防御纵深，无需重新编译 C++ DLL）
- 修复 PeBoundaryTests 数组越界 bug
- 修复 Loader C4244 (uint64→uint32 截断) 编译警告
- 修复 Loader C4146 (无符号取负) 编译警告
- 修复 lzma_dec.c 重复注释行
- 补充 VFS_ERR_NO_MEMORY (6009) 缺失错误码

### Changed
- VfsDirNode 增加 FullPath 属性缓存；重写 Equals/GetHashCode 基于路径比较
- TempFileHelper 增加测试名前缀和进程 ID，缓解 E2E 并行竞争
- csproj 添加 PackageLicenseExpression MIT 声明
- .gitignore 增加 TestResults/ 和 .claude/ 忽略规则

### Added
- PeBoundaryTests: 8 个 PE 畸形输入边界测试用例
- .github/workflows/ci.yml: GitHub Actions CI/CD 流水线
- LICENSE: MIT 开源许可证文件

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
