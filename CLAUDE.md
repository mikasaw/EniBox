# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test Commands

```bash
# Build .NET (C#) project (Release)
dotnet build src/EniBox.GUI/EniBox.GUI.csproj -c Release

# Build C++ native DLLs (PeTool, Loader) — requires MSVC
msbuild src/EniBox.PeTool/EniBox.PeTool.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild src/EniBox.Loader/EniBox.Loader.vcxproj /p:Configuration=Release /p:Platform=x64

# Run all tests
dotnet test tests/EniBox.Tests/EniBox.Tests.csproj -c Release

# Run a single test class
dotnet test --filter "FullyQualifiedName~PeBoundaryTests"

# Run a single test method
dotnet test --filter "FullyQualifiedName~PackService_TruncatedPeHeader_ReturnsInvalidPe"

# Publish single-file output
dotnet publish src/EniBox.GUI/EniBox.GUI.csproj -c Release -r win-x64
```

## Three-Layer Architecture

```
EniBox.GUI (C# WPF)  ←P/Invoke→  EniBox.PeTool (C/C++ DLL)  ←embedded→  EniBox.Loader (C/C++ DLL)
```

**EniBox.GUI** — WPF application + CLI entry. Core services:
- `PackService` — orchestrates pack flow: parse PE → build VFS → compress → modify PE → save
- `VfsBuilder` — builds VFS directory tree and binary image from dependency files
- `LzmaCompressor` — LZMA compress/decompress via third-party lib
- `PeToolInterop` — P/Invoke wrapper for the native PeTool DLL

**EniBox.PeTool** — Native C/C++ DLL for PE file manipulation. Exported via `pe_exports.h`:
- `PE_Open/Close` — open/close PE context (reads file, validates headers)
- `PE_AddSection` — add a new section (`.enibox`) to the target PE
- `PE_MergeImports` — merge Loader DLL import into the target
- `PE_ProcessTLS` — handle TLS callback interception
- `PE_Save` — write modified PE to disk

**EniBox.Loader** — Native C/C++ DLL embedded into the output EXE's `.enibox` section. Loaded at runtime:
- `loader_main.c` — DllMain entry, extracts VFS data and self from `.enibox` section
- `vfs_runtime.c` — VFS initialization, file lookup, read operations
- `vfs_hashtable.c` — path-based hash table for O(1) VFS file lookup
- `hook_fileapi.c` — hooks `CreateFileW/A`, `ReadFile`, `WriteFile`, `SetFilePointer`
- `hook_ntapi.c` — hooks `NtCreateFile`, `NtOpenFile`, `NtReadFile`
- `hook_registry.c` — hooks `RegOpenKeyEx`, `RegQueryValueEx`, etc.
- `hook_process.c` — hooks `CreateProcessW/A` for child process injection
- `inject.c` — 3-level injection strategy: `QueueUserAPC` → `NtCreateThreadEx` → `CreateRemoteThread`
- `lzma_dec.c` — embedded LZMA decompressor for VFS data

## Pack Flow

1. Parse source PE (PeTool) → detect architecture (x86/x64)
2. Build VFS directory tree from dependency files (VfsBuilder)
3. Compress VFS metadata + data region (LZMA)
4. Select and embed Loader DLL matching target architecture
5. Combine VFS data + Loader DLL into `.enibox` section payload
6. PeTool: add `.enibox` section, merge import (Loader.dll), process TLS, set entry point stub
7. Save modified PE to output path

## Runtime Flow (packed EXE)

1. OS loads EXE → Loader's DllMain fires (`DLL_PROCESS_ATTACH`)
2. Loader reads `.enibox` section → extracts VFS data + its own DLL
3. Writes Loader DLL to `%TEMP%\EniBox-{pid}-{rnd}\` (CRC32 verified)
4. LoadLibrary on the extracted Loader DLL → hooks install
5. Original entry point runs → file/registry/process calls intercepted by hooks

## VFS Path Matching

VFS uses normalized (UPPERCASE, `\` separators) full path strings in a FNV-1a hash table.
- `VFS_NormalizePath()`: lowercase→UPPERCASE, `/`→`\`, strips leading `\`
- Both `CreateFileA` and `CreateFileW` are hooked directly (not just NtCreateFile)
- `CreateFileA(W)` → `VFS_LookupFile(path)` → match → return VFS handle without touching disk
- If no match → falls through to original API (passthrough)

## Test Structure (172 tests)

| Category | File | What it tests |
|----------|------|---------------|
| Unit | `VfsModelTests.cs` | CRC32, VFS header/entry serialization, model defaults |
| Unit | `VfsDataStructureTests.cs` | VfsHeader/Crc32 serialization, validation |
| Unit | `PackConfigurationTests.cs` | Config/ErrorCode/PeInfo model behavior |
| Unit | `PeBoundaryTests.cs` | 8 malformed PE inputs (no native crash) |
| Integration | `VfsBuilderTests.cs` / `IntegrationTests.cs` | VFS build + LZMA roundtrip |
| Integration | `PackService*.cs` | Input validation, Moq isolation, exception handling |
| Integration | `PeToolInteropTests.cs` | P/Invoke struct sizes, error codes |
| E2E | `E2EPackTests.cs` | Full pack of fc.exe, run packed exe, VFS passthrough |
| E2E | `E2EEnhancedTests.cs` | Loader extraction, special paths, repack, multi-file stress |
| E2E | `E2EPackedRuntimeTests.cs` | VFS file read through packed exe, VFS-only file read |
| E2E | `E2ERuntimeTests.cs` | FileChecker/RegChecker/SubProc helper tools |
| CodeReview | `*CodeReviewTests.cs` | Source file existence, function signatures |

## Test Infrastructure

- **E2ETestBase** — base class for E2E tests: `PackExeAsync()`, `RunPackedExe()`, `IsPeToolAvailable`
- **TempFileHelper** — per-test temp files with test-name prefix (mitigates parallel race)
- **ProcessRunner** — runs external processes with timeout, captures output
- **TestExeBuilder** — locates pre-compiled helpers (`tests/TestHelpers/{name}/publish/{name}.exe`)
- **SynchronousProgress\<T\>** — sync IProgress for xunit tests (no SynchronizationContext)

## Known Issues

- **E2E parallel race**: Some E2E tests (FileChecker reads existing file, EmptyDirectory) fail when run in full parallel; pass in isolation. TempFileHelper uses test-name+pid prefix to mitigate.
- **fc.exe STATUS_STACK_BUFFER_OVERRUN**: Certain Windows system binaries crash when packed (Loader compatibility limitation). Core VFS read functionality unaffected.
- **C++ DLL rebuild**: MSBuild/VS2022 required for native DLL changes. Without it, C# side pre-validation (`TryValidatePeStructure` in `PackService.cs`) provides defense-in-depth.
