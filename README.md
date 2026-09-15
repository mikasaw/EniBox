# EniBox

English | [简体中文](README.zh-CN.md)

**Virtual File Box packer** — bundles a Windows EXE with its dependency files into a single
self-contained executable that transparently resolves those files at runtime through a
virtual file system (VFS).

[![CI](https://github.com/mikasaw/EniBox/actions/workflows/ci.yml/badge.svg)](https://github.com/mikasaw/EniBox/actions/workflows/ci.yml)
![.NET 8.0](https://img.shields.io/badge/.NET-8.0-blue)
![Windows x64](https://img.shields.io/badge/Windows-x64-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Tests](https://img.shields.io/badge/Tests-176_✔️-brightgreen)

## Features

- **Single-file output** — packs an EXE + dependency DLLs + data files into one `.enibox` executable
- **Virtual file system (VFS)** — redirects file access at runtime via Win32 file API hooks; no extraction to disk needed
- **LZMA compression** — VFS data is LZMA-compressed to reduce output size
- **Registry virtualization (experimental)** — preset registry values at pack time (`--registry-virtualization` + programmatic API); reads inside the packed program hit the virtual registry, writes stay process-local (no persistence, no pollution of the real registry); unpreset keys pass through untouched
- **Child VFS inheritance (experimental)** — non-packed children spawned by a packed program are automatically injected with the loader and inherit the parent's VFS view via a VfsLink handshake; packed children use their own loader (no interference)
- **Hardened extraction** — the loader DLL is extracted into a randomized per-PID directory with CRC32 integrity verification and exclusive WRITE_THROUGH writes
- **CLI + GUI** — both a graphical interface and a command-line interface
- **Localization** — Chinese (zh-CN) and English (en-US) UI

## Getting Started

### Prerequisites

- Windows 10/11 (x64)
- .NET 8.0 SDK
- Visual Studio with the **MSVC v145 toolset** (verified on VS 18 Insiders; other VS versions require adjusting `PlatformToolset` in the vcxproj files) and the Windows 10 SDK (10.0.26100.0)

### Build

```bash
# Build the native DLLs (PeTool + Loader)
msbuild src/EniBox.PeTool/EniBox.PeTool.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild src/EniBox.Loader/EniBox.Loader.vcxproj /p:Configuration=Release /p:Platform=x64

# Build the .NET project
dotnet build src/EniBox.GUI/EniBox.GUI.csproj -c Release
```

### Publish

```bash
dotnet publish src/EniBox.GUI/EniBox.GUI.csproj -c Release -r win-x64
```

Produces a single self-contained executable in the `publish/` directory.

### Run the Tests

```bash
# Full suite
dotnet test tests/EniBox.Tests/EniBox.Tests.csproj -c Release

# Filter by category
dotnet test --filter "FullyQualifiedName~PeBoundaryTests"
dotnet test --filter "FullyQualifiedName~PackedVfsRuntimeTests"
```

## Usage

### GUI Mode

Run `EniBox.exe`, pick the source EXE and output path, add dependency files, and click pack.

### CLI Mode

```bash
EniBox.exe --cli --source <source-exe> --output <output-exe> [options]
```

See [CLI.md](CLI.md) for the full CLI reference (Chinese).

## Project Layout

```
EniBox/
├── .github/workflows/      # CI/CD pipeline (GitHub Actions)
├── src/
│   ├── EniBox.GUI/          # WPF app + CLI entry point
│   │   ├── Program.cs       # Entry point (GUI/CLI dual mode)
│   │   ├── Services/        # Packing service, CLI runner, compressor
│   │   ├── Models/          # Data models (VFS/PE/config/error codes)
│   │   ├── Interop/         # PeTool DLL P/Invoke interop
│   │   ├── ViewModels/      # MVVM view models
│   │   └── Views/           # WPF views
│   ├── EniBox.PeTool/       # C/C++ PE modification DLL
│   │   └── src/             # Import merge, TLS handling, section add
│   └── EniBox.Loader/       # C/C++ VFS runtime loader DLL
│       ├── src/             # File/registry/process hooks, VFS runtime, LZMA decode
│       ├── include/         # Headers
│       └── deps/MinHook/    # vendored MinHook (API hooking, includes the HDE disassembler)
└── tests/
    ├── EniBox.Tests/        # xUnit test project (176 tests)
    │   ├── E2E/             # End-to-end tests (pack + run + VFS + registry + subprocess)
    │   ├── PackService/     # Pack service tests (mock + exception + input)
    │   ├── Unit/            # Model/VFS/compression/interop unit tests
    │   ├── Boundary/        # Malformed PE input boundary tests
    │   └── TestInfrastructure/  # Test infrastructure (ProcessRunner, TempFileHelper, etc.)
    └── TestHelpers/         # Test helpers
        ├── VfsTest/          # Native C VFS self-check (9 runtime subtests)
        ├── FileChecker/      # C# file-read verification
        ├── RegChecker/       # Registry virtualization verification
        └── SubProcHost/      # Sub-process host/child pair
```

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) (Chinese) for the detailed design.

### Packing Pipeline

1. **Parse the source PE** — read architecture, sections and imports via the PeTool DLL
2. **Build the VFS** — organize dependency files into a virtual file system image (directory tree + file data)
3. **Compress** — LZMA-compress the VFS metadata and data region
4. **Modify the PE** — add the `.enibox` section, write an entry-point bootstrap, and ship the loader DLL alongside
5. **Write output** — emit a single-file EXE carrying the loader and VFS data

### Runtime Pipeline

1. **Loader initialization** — the entry-point bootstrap side-loads the loader DLL, which parses the VFS blob from the `.enibox` section (CRC32 verified)
2. **Hook installation** — Win32 file APIs are hooked (CreateFileA/W, ReadFile, GetFileSize, SetFilePointer, GetFileAttributes, etc.)
3. **Transparent redirection** — file access from the packed program is intercepted; VFS matches are served from memory, everything else passes through to the real file system
4. **Child VFS inheritance (experimental)** — non-packed children are injected with the loader and receive the parent's VFS via VfsLink; packed children and system programs (cmd.exe etc.) are not injected (see Known Limitations)

## Known Limitations

Please read this before using the tool in anger:

- **x64 only** — 32-bit PEs are explicitly rejected at pack time
- **The VFS is read-only** — writes to VFS files are denied; if the packed program writes config/logs into "its own directory" at runtime, redirect them to a writable location via arguments
- **Child inheritance boundaries** — system programs (cmd.exe, conhost.exe, anything under System32) are not injected and do not inherit the VFS; an injected child holds a read-only copy of the parent's VFS (it remains usable after the parent exits); grandchild inheritance is not supported yet
- **Registry virtualization is experimental** — preset reads and process-local write isolation have E2E coverage; write persistence is not supported yet
- **Limited compatibility validation** — regression currently targets Windows 11 (26200, Insider) x64; Windows 10 is a supported target but has limited coverage

## Test Coverage (176 tests)

| Category | Count | Scope |
|----------|:-----:|-------|
| Unit tests | ~80 | VFS struct serialization, CRC32, models/config/error codes, stub machine code |
| Integration tests | ~30 | VFS build, LZMA round-trip, PeTool P/Invoke, CLI arguments |
| E2E tests | 27 | Pack pipeline, packed runtime, VFS file reads, loader extraction, special paths |
| Boundary tests | 8 | Malformed PE inputs (truncated/corrupt/empty/random/SizeOfOptionalHeader=0) |
| Code review tests | ~20 | Source file presence, function signatures, architecture detection logic |

**Key E2E scenario**: pack a program with an embedded file → run it → read data that
**exists only inside the VFS** (nowhere on disk) through the hooks.

## Third-Party Components

This project vendors MinHook (BSD) and HDE (BSD), and consumes LZMA-SDK and other NuGet
packages — see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for license notices.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) (Chinese).

## License

[MIT](LICENSE) — see the LICENSE file; third-party components are subject to their own
licenses (see notices above).
