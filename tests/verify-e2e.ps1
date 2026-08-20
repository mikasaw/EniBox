<#
.SYNOPSIS
    EniBox v0.5.1 端到端验证脚本（MIT-232）

.DESCRIPTION
    把 E2E-01/02/03 的 5 步流程封装为单条命令：native build → GUI build →
    tests build → dotnet test。汇总 TRX 报告，按退出码契约返回：
      0 = 全部通过
      1 = 有失败测试
      2 = 缺前置（MSBuild / dotnet / EniBox.PeTool.dll）
      3 = 有 skipped 测试（不符合"全跑通"预期）

.PARAMETER SkipNativeBuild
    跳过 MSBuild 编译 PeTool/Loader DLL，假设 native 产物已在位

.PARAMETER NoTest
    只 build 不跑 test（用于 CI 准备阶段）

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1 -SkipNativeBuild

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-e2e.ps1 -NoTest
#>

[CmdletBinding()]
param(
    [switch]$SkipNativeBuild,
    [switch]$NoTest
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $root

# ============================================================
# 前置检查
# ============================================================

$msbuildPath = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuildPath)) {
    # fallback: 探测 vswhere
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -property installationPath 2>$null
        if ($vs) {
            $candidate = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $candidate) { $msbuildPath = $candidate }
        }
    }
}
if (-not (Test-Path $msbuildPath)) {
    Write-Host '[FATAL] 找不到 MSBuild（Visual Studio 未安装？）' -ForegroundColor Red
    Write-Host '  期望路径: C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe'
    exit 2
}

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    Write-Host '[FATAL] 找不到 dotnet CLI' -ForegroundColor Red
    exit 2
}

# DOTNET_ROOT：本机 hostfxr 在 C:\Users\www\dotnet（用户目录，非 system）
if (-not $env:DOTNET_ROOT -or -not (Test-Path $env:DOTNET_ROOT)) {
    $userDotnet = 'C:\Users\www\dotnet'
    if (Test-Path $userDotnet) {
        $env:DOTNET_ROOT = $userDotnet
    } else {
        Write-Host '[WARN] DOTNET_ROOT 未设置且未找到 C:\Users\www\dotnet，testhost 可能启动失败' -ForegroundColor Yellow
    }
}
# PATH 前置 dotnet 让 testhost 找到
$dotnetExeDir = Split-Path -Parent (Get-Command dotnet).Source
if ($env:PATH -notlike "*$dotnetExeDir*") {
    $env:PATH = "$dotnetExeDir;$env:PATH"
}

Write-Host ''
Write-Host '=== EniBox v0.5.1 E2E 验证脚本 ===' -ForegroundColor Cyan
Write-Host "  Root:        $root"
Write-Host "  MSBuild:     $msbuildPath"
Write-Host "  DOTNET_ROOT: $env:DOTNET_ROOT"
Write-Host "  SkipNative:  $SkipNativeBuild"
Write-Host "  NoTest:      $NoTest"
Write-Host ''

# ============================================================
# Step 1: native build (PeTool + Loader)
# ============================================================

if (-not $SkipNativeBuild) {
    Write-Host '=== Step 1/4: 编译 native DLL ===' -ForegroundColor Cyan
    & $msbuildPath (Join-Path $root 'src/EniBox.PeTool/EniBox.PeTool.vcxproj') /p:Configuration=Release /p:Platform=x64 /v:minimal
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] EniBox.PeTool 编译失败 (exit=$LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
    & $msbuildPath (Join-Path $root 'src/EniBox.Loader/EniBox.Loader.vcxproj') /p:Configuration=Release /p:Platform=x64 /v:minimal
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] EniBox.Loader 编译失败 (exit=$LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host '=== Step 1/4: 跳过 native build (-SkipNativeBuild) ===' -ForegroundColor DarkGray
}

# 前置 DLL 检查（无论是否 SkipNativeBuild 都要查）
$peToolNative = Join-Path $root 'src/EniBox.PeTool/x64/Release/EniBox.PeTool.dll'
$loaderNative = Join-Path $root 'src/EniBox.Loader/x64/Release/EniBox.Loader.dll'
if (-not (Test-Path $peToolNative)) {
    Write-Host "[FATAL] 缺前置: $peToolNative" -ForegroundColor Red
    Write-Host '  跑一次不带 -SkipNativeBuild 让 MSBuild 编译，或确认 E2E-01 已完成'
    exit 2
}
if (-not (Test-Path $loaderNative)) {
    Write-Host "[FATAL] 缺前置: $loaderNative" -ForegroundColor Red
    exit 2
}

# ============================================================
# Step 2: GUI build (会自动复制 native DLL 到 bin)
# ============================================================

Write-Host ''
Write-Host '=== Step 2/4: 编译 GUI ===' -ForegroundColor Cyan
& dotnet build (Join-Path $root 'src/EniBox.GUI/EniBox.GUI.csproj') -c Release --nologo -v:minimal
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] EniBox.GUI 编译失败 (exit=$LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

# GUI build 后 native DLL 应已复制到 bin/Release/net8.0-windows/win-x64/
$peToolInBin = Join-Path $root 'src/EniBox.GUI/bin/Release/net8.0-windows/win-x64/EniBox.PeTool.dll'
if (-not (Test-Path $peToolInBin)) {
    Write-Host "[FATAL] GUI build 后未生成 $peToolInBin" -ForegroundColor Red
    exit 2
}

# ============================================================
# Step 3: Tests build
# ============================================================

Write-Host ''
Write-Host '=== Step 3/4: 编译测试 ===' -ForegroundColor Cyan
& dotnet build (Join-Path $root 'tests/EniBox.Tests/EniBox.Tests.csproj') -c Release --nologo -v:minimal
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] EniBox.Tests 编译失败 (exit=$LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

# Tests build 后 native DLL 应已复制到 bin/Release/net8.0-windows/
$peToolInTestsBin = Join-Path $root 'tests/EniBox.Tests/bin/Release/net8.0-windows/EniBox.PeTool.dll'
if (-not (Test-Path $peToolInTestsBin)) {
    Write-Host "[FATAL] Tests build 后未生成 $peToolInTestsBin" -ForegroundColor Red
    exit 2
}

# ============================================================
# Step 4: dotnet test
# ============================================================

if ($NoTest) {
    Write-Host ''
    Write-Host '=== Step 4/4: 跳过 (-NoTest) ===' -ForegroundColor DarkGray
    Write-Host ''
    Write-Host '=== 验证完成（仅 build，未跑 test）===' -ForegroundColor Green
    exit 0
}

Write-Host ''
Write-Host '=== Step 4/4: 跑测试 ===' -ForegroundColor Cyan
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$trxDir = Join-Path $root 'tests/EniBox.Tests/TestResults'
if (-not (Test-Path $trxDir)) { New-Item -ItemType Directory -Path $trxDir -Force | Out-Null }
$trxFile = Join-Path $trxDir "verify-e2e-$timestamp.trx"

# --no-build: 避免 tests build 覆盖 native DLL
& dotnet test (Join-Path $root 'tests/EniBox.Tests/EniBox.Tests.csproj') `
    -c Release `
    --no-build `
    --nologo `
    --logger "trx;LogFileName=$trxFile" `
    --logger 'console;verbosity=normal'
$testExitCode = $LASTEXITCODE

Write-Host ''
Write-Host '=== TRX 报告解析 ===' -ForegroundColor Cyan

# 找最新生成的 trx 文件（可能因 logger 行为不在预期路径）
if (-not (Test-Path $trxFile)) {
    $latestTrx = Get-ChildItem -Path $trxDir -Filter '*.trx' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latestTrx) {
        $trxFile = $latestTrx.FullName
        Write-Host "  (fallback 到最新 TRX: $trxFile)" -ForegroundColor DarkGray
    }
}

if (-not (Test-Path $trxFile)) {
    Write-Host "[FAIL] TRX 报告未生成 — dotnet test 退出码 $testExitCode" -ForegroundColor Red
    exit 1
}

# 解析 TRX: 提取 Counters
[xml]$trx = Get-Content $trxFile
$counters = $trx.TestRun.ResultSummary.Counters
$total = [int]$counters.total
$executed = [int]$counters.executed
$passed = [int]$counters.passed
$failed = [int]$counters.failed
# xUnit.SkippableFact 在 TRX 里报 outcome="NotExecuted"（不是 Skipped）
$skipped = ($trx.TestRun.Results.UnitTestResult |
    Where-Object { $_.outcome -eq 'NotExecuted' } |
    Measure-Object).Count
$outcome = $trx.TestRun.ResultSummary.outcome

Write-Host "  Outcome:  $outcome"
Write-Host "  Total:    $total"
Write-Host "  Executed: $executed"
Write-Host "  Passed:   $passed"
Write-Host "  Failed:   $failed"
Write-Host "  Skipped:  $skipped"
Write-Host "  TRX:      $trxFile"
Write-Host ''

# 失败用例详情
if ($failed -gt 0) {
    Write-Host '=== 失败用例（前 10 个）===' -ForegroundColor Red
    $failedTests = $trx.TestRun.Results.UnitTestResult |
        Where-Object { $_.outcome -eq 'Failed' } |
        Select-Object -First 10
    foreach ($t in $failedTests) {
        Write-Host "  [FAIL] $($t.testName)" -ForegroundColor Red
        if ($t.Output -and $t.Output.ErrorInfo) {
            $msg = $t.Output.ErrorInfo.Message
            if ($msg -and $msg.Length -gt 0) {
                $firstLine = ($msg -split "`n")[0]
                if ($firstLine.Length -gt 200) { $firstLine = $firstLine.Substring(0, 200) + '...' }
                Write-Host "         $firstLine" -ForegroundColor DarkRed
            }
        }
    }
    Write-Host ''
}

# ============================================================
# 退出码契约
# ============================================================

if ($failed -gt 0) {
    Write-Host '=== 验证失败 ===' -ForegroundColor Red
    Write-Host '  有 failed 测试，详见上方列表与 TRX 报告'
    exit 1
}

if ($skipped -gt 0) {
    Write-Host '=== 验证未全跑通 ===' -ForegroundColor Yellow
    Write-Host "  有 $skipped 个 skipped 测试（不符合'全跑通'预期）"
    Write-Host '  可能原因: PeTool 探测失败 / Helper 缺失 / Loader 提取失败'
    Write-Host '  参考 tests/E2E-RUNBOOK.md §6 故障排查'
    exit 3
}

if ($total -ne $executed) {
    Write-Host '=== 验证异常 ===' -ForegroundColor Yellow
    Write-Host "  Total($total) != Executed($executed)，存在未被执行的测试"
    exit 3
}

if ($testExitCode -ne 0) {
    Write-Host '=== 验证异常 ===' -ForegroundColor Yellow
    Write-Host "  dotnet test 退出码 = $testExitCode（但 TRX 中无 failed）"
    exit 1
}

Write-Host '=== 验证全部通过 ===' -ForegroundColor Green
Write-Host "  $passed / $total 测试全过，0 failed, 0 skipped"
exit 0