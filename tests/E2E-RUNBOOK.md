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

### 5.1 fc.exe 在封包后 STATUS_STACK_BUFFER_OVERRUN (0xC0000409)

**现象**: `E2E_PackedFcExe_ExitsNormally_NotHanging` 退出码 = 0xC0000409

**原因**: fc.exe 是 Windows 系统二进制，Loader 的 hook 框架（MinHook + inline trampoline）在某些代码路径上不兼容 fc.exe 的入口 stub。这是 Loader 的已知限制，不影响 VFS 文件读取等核心功能。

**验证**: 看 `Assert.False(runResult.TimedOut, ...)` 通过即可（"不挂起"是核心要求，"正常退出码"不是）。

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