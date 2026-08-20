# ============================================================
# VfsTest 构建脚本 (PowerShell 版)
# 优先用 vswhere 定位 vcvars64.bat, fallback 到已知路径
# 用法:
#   .\build.ps1                  # 默认 Debug
#   .\build.ps1 -Configuration Release
#   .\build.ps1 -Clean           # 清理后构建
# ============================================================
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

function Get-VcvarsPath {
    # 1. 环境变量优先
    if ($env:VCVARSALL -and (Test-Path $env:VCVARSALL)) { return $env:VCVARSALL }

    # 2. vswhere (VS2017+ 标准方式)
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and $installPath) {
            $candidate = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    # 3. 已知路径 fallback (按优先级)
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat',
        'G:\vs2026\VC\Auxiliary\Build\vcvars64.bat',
        'F:\vs2026\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    )
    foreach ($p in $candidates) {
        if (Test-Path $p) { return $p }
    }

    throw "Could not locate vcvars64.bat (env / vswhere / candidates)"
}

$vcvars = Get-VcvarsPath
Write-Host "Using vcvars64.bat: $vcvars"

# 清理 (可选)
if ($Clean) {
    foreach ($f in @('VfsTest.exe', 'VfsTest.obj', 'vc140.pdb')) {
        if (Test-Path $f) { Remove-Item $f -Force }
    }
    if (Test-Path 'publish\VfsTest.exe') { Remove-Item 'publish\VfsTest.exe' -Force }
    Write-Host "Cleaned."
}

# 调用 vcvars64 然后编译
# 注意: cmd /c 是必须的, vcvars 是 .bat 文件
$cmdLine = "`"$vcvars`" && cl.exe vfstest.c /Fe:VfsTest.exe advapi32.lib /nologo"
Write-Host "Running: cmd /c $cmdLine"
cmd /c $cmdLine
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed with exit code $LASTEXITCODE"
}

# 复制到 publish
if (-not (Test-Path 'publish')) {
    New-Item -ItemType Directory -Path 'publish' | Out-Null
}
Copy-Item -Path 'VfsTest.exe' -Destination 'publish\' -Force
Write-Host "Build success: VfsTest.exe -> publish\"