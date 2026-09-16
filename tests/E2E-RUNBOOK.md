# EniBox E2E 真跑指南

> 适用版本: EniBox v0.5.1  
> 本文档说明 E2E 测试为何不能"裸跑"、如何区分 Skip vs Pass / Fail、以及常见故障的根因清单。  
> 最后更新: 2026-08-20 (MIT-231)

## 1. 为何当前 E2E 测试不能"裸跑"

EniBox E2E 测试是端到端验证，依赖三个完整链条：

| 依赖 | 作用 | 缺失时表现 |
|------|------|-----------|
| `EniBox.PeTool.dll` (native) | 解析/修改 PE 头部，封包必备 | 全部 E2E 测试 → **Skipped** |
| `tests/TestHelpers/*/publish/*.exe` | 测试辅助程序 (VfsTest/FileChecker 等) | 对应测试 → **Skipped** |
| `EniBox.Loader.dll` (native, 通过 PeTool 嵌入 .enibox section) | VFS 运行时 + 三级注入 | 封包后运行 → **失败/挂起** |

E2E 测试用 `Xunit.SkippableFact` + `Skip.IfNot()` 在依赖缺失时显式 Skip（不是静默 Pass）——这是 MIT-230 改造的语义。

## 2. 前置条件

### 2.1 软件

| 软件 | 版本 | 验证命令 |
|------|------|---------|
| .NET SDK | 8.0 (含 WindowsDesktop) | `dotnet --list-sdks` |
| .NET Runtime | 8.0 | `dotnet --list-runtimes` |
| Visual Studio | 2026 v145 (或 Insiders) | MSBuild 路径需含 v145 |
| Windows SDK | 10.0.26100.0 (默认) | 看 VCToolsInstallDir |

### 2.2 路径

- `DOTNET_ROOT` 指向本机 .NET hostfxr (本机 `C:\Users\www\dotnet`)
- vcvars64.bat 在 `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\` (Insiders) 或 `F:\vs2026\` / `G:\vs2026\` (本地稳定版)

### 2.3 已编译产物

| 产物 | 路径 | 验证 |
|------|------|------|
| `EniBox.PeTool.dll` | `src/EniBox.PeTool/bin/x64/Release/` | `Test-Path` |
| `EniBox.Loader.dll` | `src/EniBox.Loader/bin/x64/Release/` | `Test-Path` |
| `EniBox.GUI.dll` | `src/EniBox.GUI/bin/Release/net8.0-windows/` | `Test-Path` |
| `VfsTest.exe` | `tests/TestHelpers/VfsTest/publish/` | `Test-Path` |

## 3. 五步真跑流程 (对应 E2E-01)

按以下顺序执行命令（每步依赖前一步产物）：

### Step 1: 编译 native DLLs

```cmd
msbuild src\EniBox.PeTool\EniBox.PeTool.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild src\EniBox.Loader\EniBox.Loader.vcxproj /p:Configuration=Release /p:Platform=x64
```

预期输出: `Build succeeded. 0 Error(s), 0 Warning(s)`

### Step 2: 编译 C# 主项目 (含 P/Invoke stub)

```cmd
dotnet build src\EniBox.GUI\EniBox.GUI.csproj -c Release
```

预期输出: `Build succeeded. 0 Error(s), 0 Warning(s)`

### Step 3: 编译 C 原生测试 helper (VfsTest)

```cmd
cd tests\TestHelpers\VfsTest
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

或直接用 cmd 脚本:

```cmd
cd tests\TestHelpers\VfsTest
.\build.cmd
```

预期输出:
```
Using vcvars64.bat: C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat
Build success: VfsTest.exe -> publish\
```

### Step 4: 编译 C# 测试工程

```cmd
dotnet build tests\EniBox.Tests\EniBox.Tests.csproj -c Release
```

预期输出: 0 Error(s), 0 Warning(s)

### Step 5: 跑 E2E 测试 (含 VfsTest 集成验证)

```cmd
dotnet test tests\EniBox.Tests\EniBox.Tests.csproj -c Release --no-build
```

预期输出:
```
Passed!  - Failed: 0, Passed: N, Skipped: M, Total: N+M
```

## 4. dotnet test 输出判读表

| 输出模式 | 含义 | 处理 |
|----------|------|------|
| `Passed  <TestName> [<duration>s]` | 测试真跑并通过 | 无需处理 |
| `Skipped <TestName>` + `Skip.IfNot` 异常消息 | 依赖缺失（PeTool / Helper）| 见 §6 故障排查 |
| `Failed  <TestName>` + `Expected ... Actual ...` | 断言失败 | 检查 VFS 数据或封包配置 |
| `Failed  <TestName>` + `Process timed out` | 封包程序挂起 | 检查 Loader 兼容性 |
| `Failed  <TestName>` + `EniBox.PeTool.dll not found` | 依赖未编译 | 重跑 Step 1 |
| 0 tests run | 测试筛选过严 | 检查 `--filter` 或 `[SkippableFact]` 框架 |

### 4.1 VfsTest 真跑成功的标志

`E2E_PackedVfsTest_AllVfsFeatures` 通过时输出应包含 9 个 `CHECK:PASS:test_*` 断言：

```
Test 1: VFS File Read               → test_VfsFileRead               (需封包)
Test 2: VFS File Seek               → test_VfsFileSeek               (需封包)
Test 3: VFS File Attributes         → test_VfsFileAttributes         (需封包)
Test 4: VFS File Not Found          → test_VfsFileNotFound           (无需封包)
Test 5: VFS File Write Rejected     → test_VfsFileWriteRejected      (无需封包)
Test 6: Child Process               → test_ChildProcess              (无需封包)
Test 7: Loader Extraction Path      → test_LoaderExtractionPath      (P0-02, 需封包)
Test 8: Loader Integrity CRC32      → test_LoaderIntegrityCrc32      (P0-02, 需封包)
Test 9: Sub-Process Injection       → test_SubProcessInjectionStrategy (P0-03, 需封包)
```

任一断言缺失 → 立即看 VfsTest.exe 独立运行输出（用封包产物直接运行）。

### 4.2 P0 安全加固子测试 (Test 7/8/9) 详解

| 子测试 | 验证什么 | unpacked 行为 |
|--------|----------|----------------|
| **Test 7**: test_LoaderExtractionPath | `%TEMP%\EniBox-<pid>-<rnd>\EniBox.Loader.<pid>.<rnd>.dll` 存在,文件名含 PID | FAIL: "No EniBox-* directory" (无 Loader 提取) |
| **Test 8**: test_LoaderIntegrityCrc32 | .enibox section 嵌入式 Loader DLL 的 CRC32 与磁盘提取的 Loader DLL CRC32 一致 | FAIL: ".enibox section not found" (无 .enibox section) |
| **Test 9**: test_SubProcessInjectionStrategy | 创建子进程 cmd.exe 后,父进程 VFS 仍工作 (证明子进程注入 hook 未破坏父进程 hooks) | FAIL: "Pre-child: VFS open failed" (无 VFS hook) |

**核心设计**: Test 8 不只检查"两次读取一致",而是从当前 PE 的 .enibox section 取出嵌入式 Loader 字节,与磁盘文件对比 CRC32 — 直接验证 `loader_main.c::ExtractEmbeddedLoader` 的 `FILE_FLAG_WRITE_THROUGH` + 写后回读链路。

**Test 9 不区分三级注入**: APC → NtCreateThreadEx → CreateRemoteThread 的回退由 Loader 在 `inject.c` 实现,本测试仅验证"注入链路通畅 + 不破坏父进程",具体哪一级生效看 Loader 端 `OutputDebugStringW`。

## 5. 已知问题

### 5.1 封包产物启动即崩溃（0xC0000409 / 0xC0000005）——已全部修复（2026-09-15 二次更新）

**现象**: 封包产物启动即崩溃；cdb 分析定位为两类签名：0xC0000409（FAST_FAIL_GUARD_ICALL_CHECK_FAILURE 或 abort）与 0xC0000005。

**2026-09-15 晚间结论**: 崩溃已完全修复，`E2E` 全量 24/24 绿、封包 VfsTest 9/9 子测试全过。当晚残留的"trampoline AV"最终定位为**陈旧嵌入资源**：GUI 程序集内嵌的 `EniBox.Loader.x64.dll` 是旧版自定义 MinHook 构建（MSBuild 在原生 DLL 变更后不保证重新嵌入），崩溃现场的 `ff 25 rel32` 6 字节 detour 正是旧版格式（上游 MinHook 的 x64 detour 是 14 字节 `ff 25 00000000 + abs64`）。修复手段：
1. vendored MinHook 整体替换为上游 TsudaKageyu/MinHook（buffer.c/trampoline.c/HDE）；
2. `SelectLoaderDll` 改为磁盘优先（`Resources\EniBox.Loader.<arch>.dll` → `EniBox.Loader.<arch>.dll` → `EniBox.Loader.dll`），嵌入资源仅作回退，且无论来源都做 MZ/PE 机器类型校验，并在 CLI 进度输出中打印 SHA256 指纹前 16 位——来源与版本从此可观察。

**D3 专项现状（2026-09-15 探针重测后已恢复启用）**: 探针实证上游 MinHook 在 Win11 26200 上处理 Nt 层（NtCreateFile/NtOpenFile/NtReadFile）与进程链（CreateProcessW/A）的 trampoline 完全可用——旧"必崩"结论是陈旧嵌入 Loader 字节造成的误诊。三项探针：① Nt 层启用后 VfsTest 9/9，透传/CRC 读取全部穿过 detour；② 进程链启用后 9/9；③ 非跳过名单子进程 W/A 两族注入成功（cdb 子进程调试确认 Loader 落在子进程）。现已在 hook_manager.c 恢复启用，并实现子进程 VFS 继承：父注入前写 VfsLink 握手文件（`%TEMP%\EniBox-<pid>-<rnd>\EniBox.VfsLink`，含父镜像路径），注入的 Loader 在子进程 DllMain 中映射父镜像、拷贝 .enibox 内 VFS blob 到私有内存并初始化；封包子进程（含 .enibox 节）跳过注入以防同名模块遮蔽。E2E `E2E_PackedSubProcHost_ChildInheritsParentVfs` 覆盖全链路。注意 ShouldInjectProcess 跳过名单（cmd.exe/conhost/System32 等）内的子进程不注入、不继承 VFS。 注册表虚拟化（Phase 2）：VFS blob v2（header 52 字节，含 RegistryOffset/RegistrySize）尾部携带 'EREG' 预置值区，Loader 启动时 VReg_Preload 预载到进程内虚拟注册表。**破坏性变更**：v1 封包产物不再被新 Loader 接受（version<2 拒绝），旧产物需重新封包。预定义根键句柄比较采用低 32 位归一化（修复 SDK 符号扩展 vs 应用零扩展的失配）。

**注册表持久化（2026-09-16，B/B/A/A/A 语义）**: ① 作用域 = 前缀子树规则——预置键路径即作用域根，子树内（含运行时 RegCreateKeyEx 新建键）全部虚拟化；作用域外完全透传。② 存储 = 封包产物同目录 `<产物路径>.vreg.bin`（继承子进程经父镜像路径推导同一路径，父子读写同一份）。③ 合并 = 持久化优先——启动时 sidecar 存在且 CRC 校验通过则整体替换预置值（预置仅首启生效），缺失/损坏回退预置值。④ 并发 = 后写赢无锁（写值/新建键即整库落盘，格式 `[ENIVREG1][u32 bodySize]['EREG' body][u32 crc32]`，body 复用 'EREG' 区格式、数据按写入原始字节存取不做编码转换）。⑤ 重置 = 删除 sidecar 即回到预置值。已知边界：sidecar 明文；真实注册表已存在的同路径值在作用域内会被遮蔽且不导入。E2E `E2E_PackedRegChecker_RegistryPersistenceRoundTrip` 覆盖 写→退出→重启读回→删档重置→真实注册表零写入 全链路（RegChecker 助手 `write <v>`/`read` 模式）。并发实例读写同一 sidecar 为后写赢、读取侧以共享模式容忍写入瞬间。**删除语义（2026-09-16 B-3，验收修复后）**：RegDeleteValue/RegDeleteKey/RegDeleteTree/RegDeleteKeyEx 全部挂钩（钩子 11→19），键删除采用墓碑槽位（path[0]=0，数组索引稳定、已开句柄按 ERROR_KEY_DELETED=1018（winerror.h 实值，非 1016） 拒绝；槽位不复用，反复建/删键最终受 512 槽上限约束、进程重启恢复），序列化跳过空槽——存储即真相，删 sidecar 重置后预置值（含被删预置值）恢复。防逃逸守卫：删除**作用域根**（无存活祖先键）被拒绝 ACCESS_DENIED，防止子树退出虚拟化后写入透传真实注册表；RegDeleteTree 按真实语义删子键+值、键本身保留。BuildKeyPath 已支持虚拟父句柄路径解析（open/create/delete 以虚拟句柄为父不再落穿透）。E2E `E2E_PackedRegChecker_RegistryDeleteAndCorruptFallback` 覆盖 删值持久/子键守卫 ACCESS_DENIED/墓碑句柄 1018/根删除拒绝/损坏回退；`E2E_PackedRegChecker_MultiValueSameKey` 回归同键多预置值聚合（验收 P1：曾拆多槽致第二条起不可达）。CLI 预置值：`--registry-value KEY|NAME|TYPE|DATA`（可重复，TYPE∈SZ|EXPAND_SZ|DWORD|BINARY，SZ 文本不含 `|`，见 CLI.md）。

**2026-09-15 已修复的根因链（历史存档）**:
1. `PE_ModMergeImports` 三态逻辑全部损坏：常规 MSVC 镜像导入表无冗余空间 → 静默跳过仍返回成功（Loader 从未进导入表）；空表路径越界必返回 SECTION_FULL。
2. `.NET` 单文件宿主尾部有 bundle overlay，新节 `PointerToRawData` 按"最后节末尾"计算与 overlay 碰撞，节内容与节头错位（现按文件总长对齐）。
3. 入口点重定向到 CFG 位图之外的目标 → 线程启动即 `FAST_FAIL_GUARD_ICALL_CHECK_FAILURE`（现改为 entry-point bootstrap 方案：EP → .enibox 内 bootstrap 代码 LoadLibrary 旁置 Loader → 跳回原 EP，并清除 GUARD_CF）。
4. stub 覆盖了 vfsTotalSize 与 VFS 头（现由宿主预留 stub 洞，布局自描述：+280 ep、+284 secRVA、+288 vfsTotal、+292 loaderSize、+296 VFS）。
5. `VfsBuilder` 存储的 VFS 校验和是对"大小字段全零"的头部计算的（现先回写真实大小再计算）。
6. `PackService` 现在把 `EniBox.Loader.dll` 旁置到产物同目录（bootstrap 的 LoadLibraryA 需要）。
7. `VfsBuilder` 头部 `DataOffset` 写死 0，而 blob 实际是 `[metadata][data]` 布局，Loader 按它解析数据区 → 文件内容被读成 VFS 元数据（现写 `metadataStream.Length`）。
8. 盘根路径文件（如 `C:\file.txt`）的 `Path.GetDirectoryName` 返回 `C:\`（带尾反斜杠），与 `GetFullPath(dirNode)` 重建的 `C:` 不等 → `DirIndex=INVALID`，Loader 重建路径丢失盘符 → 查找全部脱靶（现 `TrimEnd('\\','/')` 归一）。

### 5.1.1（历史）fc.exe 在封包后 STATUS_STACK_BUFFER_OVERRUN (0xC0000409)

**现象**: `E2E_PackedFcExe_ExitsNormally_NotHanging` 退出码 = 0xC0000409

**原因**: 同上——Loader 的 hook 框架兼容性问题（2026-09-15 随 §5.1 一并修复）。

**验证**: 看 `Assert.False(runResult.TimedOut, ...)` 通过即可（"不挂起"是核心要求，"正常退出码"不是）。

### 5.1.2 Windows Defender 隔离封包测试产物（环境风险，未修复——需管理员）

**现象**: `dotnet test` 全量跑时，个别 E2E 测试偶发 `Win32Exception: 系统找不到指定的文件`（`Process.Start` 启动 `%TEMP%\EniBox-E2E-*\*.enibox` 时）。单测/过滤跑复现率低，全量并行跑更高。属随机抽风，非确定性。

**根因**: 已取证 —— `Get-MpThreatDetection` 显示 Defender 实时防护把刚生成的封包测试产物当威胁隔离（检测对象 `C:\Users\www\AppData\Local\Temp\EniBox-E2E-*\vfstest.enibox` 等）。文件在 `File.Exists` 检查之后、`CreateProcess` 之前被删。封包产物带 RWX 节 + inline hook + 注入行为，天然高启发式评分。

**处置（需管理员 PowerShell，非管理员会报"权限不足"）**:
```powershell
Add-MpPreference -ExclusionPath 'G:\AITest\enibox'
Add-MpPreference -ExclusionPath "$env:TEMP\EniBox-E2E-*"
```
CI（GitHub Actions Windows runner）默认无实时防护隔离此类文件，不受影响。

### 5.1.3 真实第三方应用冒烟（2026-09-16）

开发机（Win11 26200 x64）实测，DiagPack 封包、默认配置（子进程注入开）：

| 应用 | 版本 | 场景 | 结果 |
|---|---|---|---|
| curl.exe (System32) | 8.21.0 | `--version` / `--help` / `curl -o <file> file:///<file>` 真实下载落盘 | 全部 ✓，版本横幅与原生逐字节一致，下载文件内容正确 |
| git.exe (mingw64/bin 主二进制 + 全套 DLL 便携布局) | 2.55.0.windows.3 | `--version` / 真实仓库内 `status --short` | ✓ 版本一致；仓库内枚举语义正确（干净树输出为空，非仓库目录报 not a git repository） |

注意事项：
- `Git\cmd\git.exe` 是安装根自定位的包装器，封包后移出安装目录会报
  "Top-level not found"——这是包装器自身的预期行为，请封包
  `mingw64\bin\git.exe`（连同其 DLL 依赖）并保持便携布局。
- 更多应用（写配置类、插件目录类）待补。

### 5.1.4 并行全量 0xC0000005 取证结论（2026-09-16，T-C 时间盒）

曾观察到并行全量下封包产物退出码 0xC0000005 一次。配置 WER LocalDumps
（HKCU 免管理员）后连跑三轮全量取证：

- 三轮共 3-5 个失败**全部为启动期 Win32Exception（文件在
  File.Exists 与 CreateProcess 之间被删）**，Get-MpThreatDetection
  时间戳与失败逐次吻合（Defender 实时隔离），无一次运行期崩溃、
  无新 dump 产生。
- 结论：当前失败集 100% 为 §5.1.2 环境问题；0xC0000005 为孤例
  （疑同类加载竞争），重启用 Nt/进程链 hook 无并发失稳证据。
- 加管理员排除项（§5.1.2 命令）后此类失败应清零。

### 5.1.5 W 族注入在 CI runner 上未生效（开放问题，2026-09-16）

CI（windows-latest / Server 2022 / v143 构建）run 35015669441：`E2E_PackedSubProcHost_ChildInheritsParentVfs`
中 A 族子进程继承成功，**W 族子进程 CHILD_VFS:FAIL (error=2)**——子进程正常跑但无 VFS hook
（TRX 实证，见 verify-e2e-trx artifact）。本地 26200 双族 100% 通过。

两个嫌疑方向（下一轮带证据排查）：
1. `Inject_DetectBestMethod` 在 Server 2022 选了 APC 方法，user APC 对挂起初始线程的
   交付时机在 .NET apphost 上不可靠 → 可试强制 CreateRemoteThread 或注入后轮询
   LoadLibrary 结果；
2. kernelbase!CreateProcessW 在 Server 2022 的内部调用路径绕过 W 导出序言
   （A 内部转 W 若走内部实现而非导出，W detour 对 A 发起的调用不触发——但本例
   W 是 host 直接调用的）。

临时缓解：该测试在 CI 失败不影响其余 26 项；修复前可视为 W 族继承待验证。

**2026-09-16 取证更新（w-inject-debug 工作流）**：关闭 runner 实时防护后，
循环 10 轮 ChildInheritsParentVfs **10/10 全过**（注入/继承全链路含 A/W 两族）。
对照时间线：唯一一次 W 族失败发生在实时防护开启的运行——结论：W 族失效与
Defender 实时扫描新注入的 Loader DLL/子进程镜像相关（扫描窗口与注入后
继承初始化竞争），非注入机制本身缺陷。缓解即 §5.1.2/上文的防护排除；
本地开发机（防护开）偶发同类失败时，Loader 按设计优雅降级（子进程无
VFS 但正常运行），诊断文件 %TEMP%\EniBox_diag_<pid>.log 可定位失败步骤。
调查关闭，诊断点永久保留。

### 5.1.6 封包产物在部分 runner 宿主机启动即 0xC0000005（开放问题，2026-09-16 B-2 取证）

**现象**：2026-09-16 15:40 UTC 前后起，CI 测试 job 逐轮一绿一红；失败轮中封包产物
（fc_vfs_only.enibox / cmd_loader_test.enibox 等）启动即 `0xC0000005`（-1073741819），
stdout 为空。同一天 06:41/07:35 UTC **相同树哈希内容多次全绿**（树哈希
64fc3330… 对比验证），源码、编译器（MSVC 14.44.35207）、镜像标签均无差异。

**取证链（ci.yml WER + diag 双取证点）**：
1. WER Application Error（事件 ID 1000）：`Faulting module name: unknown,
   version 0.0.0.0`，故障地址 ~0x11ad0（极低地址跳转签名）。
2. **决定性交叉证据**：崩溃进程 PID（0x830/0x1698/0x1be0）在 `%TEMP%` 中
   **均无对应 EniBox_diag_<pid>.log**，而同轮成功的封包进程全部有
   `dllmain attach`——即 **Loader DLL 的 DllMain 从未在崩溃进程执行**，
   .enibox 节 VA 占位符未被修补 → bootstrap 跳到垃圾地址。
3. 进程能启动并执行到 EP（而非加载期 STATUS_DLL_NOT_FOUND 失败）→
   导入描述符被 Windows 加载器**静默跳过**（§5.1 D3 同族的 load-only
   import 处理差异），不是 DLL 解析失败。

**windows-2022 采样（workflow_dispatch ×3 + push ×1）**：3 败/1 过，与
windows-latest 无显著差异 → 排除单一镜像代因素；两代镜像宿主均受影响。
已回退测试 job 至 windows-latest（ci.yml 注释保留采样结论）。

**时间相关性**：当日 ~07:40 UTC 起出现，疑为 GitHub Windows runner 宿主机
侧服务/安全组件滚动变更改变了导入处理行为；待宿主侧回滚或稳定后自愈的可能
性存在。缓解：失败 job 重跑（当前实践）；ci.yml 的 WER+diag 取证步骤保留，
后续失败自动留证。

**产品级风险与后续方向**（P1，独立任务）：该机制意味着在部分 Windows 环境
（含用户机器若宿主组件行为类似）封包产物的导入式 Loader 加载可能被跳过。
候选根治方向：① bootstrap 自加载兜底（EP 存根检测 VA 未修补时走备用加载
路径，需扩 stub 逻辑）；② 双通道加载（导入表 + 早期侧通道如 TLS 回调/
AppInitEx 类机制评估，注意 Win11 加固约束见 §5.1）；③ 向 GitHub
actions/runner-images 报告取证材料（WER + PID↔diag 关联）请求宿主侧排查。

### 5.2 Standalone VfsTest.exe 跑出大量 FAIL

**现象**: 直接运行 `publish\VfsTest.exe`（未封包）输出 `CHECK:FAIL:test_VfsFileRead` 等。

**原因**: Standalone 模式无 VFS 运行时 hook、无 .enibox section、无 EniBox-* 临时目录。

**处理**: VfsTest 必须通过 PackService 嵌入到 .enibox 后才有意义。Standalone 仅验证编译成功。

### 5.3 Tests/TestHelpers publish/ 目录变化

`tests/TestHelpers/*/publish/` 已被 .gitignore 排除。第二次构建会覆盖，**不要 commit** 这目录里的 .exe。

## 6. 故障排查 — PeToolAvailabilityChecker 返回 false 的根因清单

`PeToolAvailabilityChecker.IsAvailable` 返回 false 时 E2E 测试全部 Skip。逐项检查：

| # | 检查项 | 命令 |
|---|--------|------|
| 1 | `EniBox.PeTool.dll` 是否存在于 `bin\x64\Release\` 或 `EniBox.GUI\bin\Release\net8.0-windows\` | `Get-ChildItem -Recurse -Filter EniBox.PeTool.dll` |
| 2 | DLL 是否能被 LoadLibrary 加载 (无缺失依赖) | `[System.Reflection.Assembly]::LoadFile("...\EniBox.PeTool.dll")` 或直接 `rundll32` 测试 |
| 3 | 是否能 GetExport `PE_Open` 函数 | `dumpbin /exports EniBox.PeTool.dll \| findstr PE_Open` |
| 4 | `EniBox.GUI.Interop.PeToolInterop.ImportEntry` 结构体在 .NET host 中是否定义 | `dotnet test --filter "PeToolInteropTests"` |
| 5 | 测试运行时工作目录是否含 native DLL | `$pwd` 应在 `tests\EniBox.Tests\bin\Release\net8.0-windows\` |

### 6.1 vcvars64.bat 找不到

`build.cmd` / `build.ps1` 自动探测:
1. `%VCVARSALL%` 环境变量
2. `vswhere.exe -latest` (VS2017+ 标准方式)
3. 已知硬编码路径 (`C:\Program Files\Microsoft Visual Studio\18\Insiders\...`, `G:\vs2026\...`, `F:\vs2026\...`, `C:\Program Files\Microsoft Visual Studio\2022\...`)

若全失败，手动设置 `VCINSTALLDIR` 或安装 VS 2022/2026。

### 6.2 dotnet test 报 "找不到 EniBox.PeTool.dll"

1. 重新跑 Step 1 (编译 native)
2. 跑 Step 2 (编译 .NET GUI，会自动复制 native DLL 到 bin 目录)
3. 用 `dotnet test --no-build` 而非 `dotnet test` 避免测试工程重新生成覆盖 native DLL

## 7. 与 Issue/MIT 跟踪

- **MIT-229**: E2E-01 补齐 native DLL
- **MIT-230**: E2E-02 改造 Skip 语义 (Skip.IfNot)
- **MIT-231**: E2E-03 P0 安全加固覆盖 (VfsTest 扩展 — 本文档对应任务)
  - VfsTest 新增 3 个子测试 (Test 7/8/9 覆盖 P0-02 DLL 提取路径 + CRC32 完整性 + P0-03 子进程注入策略)
  - `build.cmd` / `build.ps1` 支持多版本 VS 探测 (环境变量 → vswhere → 硬编码 fallback)
  - `E2EPackedRuntimeTests.cs` 追加 3 个新 CHECK:PASS 断言
- **MIT-232**: E2E-04 一键验证脚本（`verify-e2e.ps1`）

## 8. 一键验证脚本 (verify-e2e.ps1)

`tests/verify-e2e.ps1` 把上述第 3 节"五步真跑流程"封装为单条命令,自动跑完 native build → GUI build → Tests build → dotnet test,并按退出码契约汇报结果。

### 8.1 用法

```powershell
# 完整跑(推荐 CI 验证): native + GUI + Tests + 全测试
powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1

# 跳过 native build, 假设 native DLL 已在位(开发迭代时省时间)
powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1 -SkipNativeBuild

# 只 build 不跑 test(CI 准备阶段)
powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1 -NoTest
```

### 8.2 退出码契约

| 退出码 | 含义 | 何时触发 |
|--------|------|---------|
| `0` | 全部通过 | `passed == total` 且 `failed == 0` 且 `skipped == 0` |
| `1` | 有失败 | TRX 报告中 `failed > 0`,或 dotnet test 自身非 0 退出且 TRX 不可读 |
| `2` | 缺前置 | MSBuild 找不到 / dotnet 找不到 / `EniBox.PeTool.dll` 缺失 / `EniBox.Loader.dll` 缺失 / GUI build 后 `EniBox.PeTool.dll` 未复制到 bin / Tests build 后未复制到 tests bin |
| `3` | 有 skipped | `skipped > 0` 或 `total != executed`(不符合"全跑通"预期,可能 PeTool 探测失败 / Helper 缺失) |

### 8.3 输出格式

脚本输出每步骤进度(Step 1/4 ~ 4/4),结尾打印 TRX 报告解析汇总:

```
=== TRX 报告解析 ===
  Outcome:  Completed
  Total:    173
  Executed: 173
  Passed:   172
  Failed:   1
  Skipped:  0
  TRX:      ...\tests\EniBox.Tests\TestResults\verify-e2e-20260820-153000.trx
```

失败用例列表(前 10 个)带错误首行摘要。TRX 报告落在 `tests/EniBox.Tests/TestResults/verify-e2e-<时间戳>.trx`,包含完整错误信息。

**Skipped 计数细节**: xUnit.SkippableFact 的 skipped 测例在 TRX 里报 `outcome="NotExecuted"`(不是 `outcome="Skipped"`)。脚本遍历 `UnitTestResult` 节点统计 `NotExecuted` 数量作为 Skipped 值。`executed = total - NotExecuted`,所以有 skipped 时 `executed < total`,这也是退出码 3 的另一触发条件。

### 8.4 环境前置

脚本启动时会自动:
1. **MSBuild 探测**: 优先 `C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe`,降级走 `vswhere.exe -latest` 探测 VS 安装路径(由 §6.1 覆盖)
2. **dotnet CLI 探测**: 走标准 PATH
3. **DOTNET_ROOT**: 优先用现有环境变量,缺失时探测 `C:\Users\www\dotnet`(本机 hostfxr 用户目录)
4. **PATH 前置 dotnet**: 把 `dotnet.exe` 所在目录 prepend 到 PATH,让 testhost 启动 hostfxr

若 MSBuild / dotnet 找不到 → 退出码 2(缺前置)。

### 8.5 何时用什么 flag

| 场景 | 推荐命令 |
|------|---------|
| **CI 流水线 E2E 验证(最严)** | `verify-e2e.ps1` (全跑,期望退出码 0) |
| **CI 流水线 build-only gate** | `verify-e2e.ps1 -NoTest` (期望退出码 0) |
| **本地迭代(已 build 过,只重跑 test)** | `verify-e2e.ps1 -SkipNativeBuild` (省 native 编译) |
| **CI 默认禁了 native build(native DLL 由前置 stage 提供)** | `verify-e2e.ps1 -SkipNativeBuild` |
| **脚本返回 3(有 skipped)** | 看 §6 故障排查;预期 E2E 全跑通的 CI 不应得 3 |
| **脚本返回 2(缺前置)** | 检查 §2.2 路径 / §8.4 环境前置 |

### 8.6 与原生 5 步命令的关系

`verify-e2e.ps1` 不替代手工 §3,自动化流水线(尤其 `power_assert` / `expect` / CI 环境变量断言)用脚本一次到位即可;排错时仍以 §3 单步命令定位。脚本只是把命令按正确顺序串起来 + 自动解析 TRX + 加退出码契约。

## 9. CI 集成 (GitHub Actions e2e-verify job)

MIT-233 把 `verify-e2e.ps1` 接进 `.github/workflows/ci.yml`,作为第 4 个 job `e2e-verify`,与现有 `dotnet-build` / `msvc-build` / `dotnet-test` 协同。随后追加第 5 个 job `cli-mode-build`(CLI_MODE 变体的编译 + `--help` 冒烟——该变体曾因无人编译而烂掉)。

### 9.1 触发条件

与现有各 job 相同:

- `push` 到 `main` / `develop`
- `pull_request` 目标 `main`

### 9.2 依赖关系

```
dotnet-build ┐
             ├─→ e2e-verify
msvc-build  ─┘
         └──→ cli-mode-build
```

`e2e-verify` / `cli-mode-build` 的 `needs: [dotnet-build, msvc-build]` 确保 native DLLs 已被编译。但本 job 内部仍重跑一次 native build,理由 1: GitHub Actions 各 job 工作目录独立,matrix 上游 job 的 `bin/` 产物不会自动传递;理由 2: 显式重 build 让 job 自包含,便于重跑单个 job 排错。

### 9.3 步骤序列

| Step | 命令 / 工具 | 说明 |
|------|------------|------|
| 1 | `actions/checkout@v4` | 拉代码 |
| 2 | `actions/setup-dotnet@v4` (.NET 8.x) | 装 .NET SDK |
| 3 | `microsoft/setup-msbuild@v2` | 装 MSBuild(为 native build) |
| 4 | `msbuild` PeTool + Loader | 编译 native DLLs (Release/x64) |
| 5 | `dotnet build` GUI + Tests | 编译 .NET 端 + Tests |
| 6 | `verify-e2e.ps1 -SkipNativeBuild` | 跑 E2E 验证(已在 step 4 build,跳过 native) |
| 7 (失败时) | `actions/upload-artifact@v4` | 上传 TRX 报告 |

### 9.4 退出码契约映射

`verify-e2e.ps1` 的 4 个退出码在 GitHub Actions 中:

| 退出码 | 含义 | CI 行为 |
|--------|------|---------|
| `0` | 全部通过(0 failed / 0 skipped) | ✅ step success → job success |
| `1` | 有 failed 测试 / 编译失败 | ❌ step fail → job fail |
| `2` | 缺前置(MSBuild/dotnet/DLL 不在位) | ❌ step fail → job fail |
| `3` | 有 skipped(违反"全跑通"预期) | ❌ step fail → job fail |

GitHub Actions 会自动把脚本非 0 退出码 → step fail,所以无需额外 `if:` 判 exit code。所有 1/2/3 都视为 CI 不通过。

### 9.5 TRX Artifact 上传

`verify-e2e.ps1` 把 TRX 报告写到 `tests/EniBox.Tests/TestResults/verify-e2e-<时间戳>.trx`。CI 在 `e2e-verify` step 失败时自动上传:

```yaml
- name: 上传 TRX 报告 (失败时)
  if: failure()
  uses: actions/upload-artifact@v4
  with:
    name: verify-e2e-trx
    path: tests/EniBox.Tests/TestResults/verify-e2e-*.trx
    if-no-files-found: warn
```

artifact 名 `verify-e2e-trx`,路径匹配所有 `verify-e2e-*.trx`(`*` 通配)。下载后在 GitHub Actions UI 看完整 TRX,包含每个测试用例的状态、错误消息、堆栈。

### 9.6 完整 workflow 片段

```yaml
e2e-verify:
  needs: [dotnet-build, msvc-build]
  runs-on: windows-latest
  steps:
    - uses: actions/checkout@v4
    - uses: actions/setup-dotnet@v4
      with:
        dotnet-version: '8.x'
    - uses: microsoft/setup-msbuild@v2
    - name: 编译 native DLLs (PeTool + Loader)
      run: |
        msbuild src/EniBox.PeTool/EniBox.PeTool.vcxproj /p:Configuration=Release /p:Platform=x64 /p:TreatWarningsAsErrors=true
        msbuild src/EniBox.Loader/EniBox.Loader.vcxproj /p:Configuration=Release /p:Platform=x64 /p:TreatWarningsAsErrors=true
    - name: 编译 GUI + Tests
      run: |
        dotnet build src/EniBox.GUI/EniBox.GUI.csproj -c Release --nologo -v:minimal
        dotnet build tests/EniBox.Tests/EniBox.Tests.csproj -c Release --nologo -v:minimal
    - name: 跑 E2E 验证脚本 (verify-e2e.ps1)
      id: e2e
      shell: pwsh
      run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1 -SkipNativeBuild
    - name: 上传 TRX 报告 (失败时)
      if: failure()
      uses: actions/upload-artifact@v4
      with:
        name: verify-e2e-trx
        path: tests/EniBox.Tests/TestResults/verify-e2e-*.trx
        if-no-files-found: warn
```

### 9.7 为什么不直接用 dotnet-test 跑

`dotnet-test` job 已经存在,但与 `e2e-verify` 的语义边界不同:

| 维度 | dotnet-test | e2e-verify |
|------|------------|------------|
| 验证层级 | 单元测试 + 集成测试 | 端到端真跑(包括 .enibox 封包 + 进程注入) |
| 退出码契约 | 仅 0/非 0 | 0/1/2/3(区分"通过/失败/缺前置/有 skipped") |
| 跳过检测 | 仅检测 `outcome="Skipped"` | 检测 `outcome="NotExecuted"`(xUnit.SkippableFact 实际值)+ `executed != total` |
| 前置检查 | 编译失败时 dotnet test 自己报错 | 显式检查 native DLLs 是否在位,缺失时返回 2 |
| TRX 上传 | 无 | 失败时上传 artifact,便于排查 |

简单说:`dotnet-test` 适合"测一遍,绿就过";`e2e-verify` 适合"严格区分'真跑通'和'没跑'",对回归更敏感。

### 9.8 本地模拟 CI 行为

在本地复现 `e2e-verify` job 的逻辑:

```powershell
# 完整模拟 CI 跑一遍
cd G:\AITest\enibox
msbuild src/EniBox.PeTool/EniBox.PeTool.vcxproj /p:Configuration=Release /p:Platform=x64 /p:TreatWarningsAsErrors=true
msbuild src/EniBox.Loader/EniBox.Loader.vcxproj /p:Configuration=Release /p:Platform=x64 /p:TreatWarningsAsErrors=true
dotnet build src/EniBox.GUI/EniBox.GUI.csproj -c Release --nologo -v:minimal
dotnet build tests/EniBox.Tests/EniBox.Tests.csproj -c Release --nologo -v:minimal
powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1 -SkipNativeBuild
echo "exit code: $LASTEXITCODE"  # 期望: 0
```

故意触发"缺前置"场景(模拟 CI 编译失败 / DLL 缺失):

```powershell
# 把 native DLL 移走
mv src\EniBox.PeTool\x64\Release\EniBox.PeTool.dll src\EniBox.PeTool\x64\Release\EniBox.PeTool.dll.bak
powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1 -SkipNativeBuild
echo "exit code: $LASTEXITCODE"  # 期望: 2 (缺前置)
# 还原
mv src\EniBox.PeTool\x64\Release\EniBox.PeTool.dll.bak src\EniBox.PeTool\x64\Release\EniBox.PeTool.dll
```

本地确认 exit 0 / exit 2 行为后,CI 上跑同一脚本应得同一行为。