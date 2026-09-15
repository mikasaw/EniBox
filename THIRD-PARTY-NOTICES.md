# Third-Party Notices / 第三方组件声明

This project includes and depends on the following third-party components.
All license terms below are supplementary to the project's MIT license (see [LICENSE](LICENSE)).

本仓库包含及依赖以下第三方组件。下列许可条款是对项目 MIT 许可（见 [LICENSE](LICENSE)）的补充说明。

---

## Vendored source code / 随仓库分发的源码

### MinHook

- **Source**: https://github.com/TsudaKageyu/minhook (vendored copy under `src/EniBox.Loader/deps/MinHook/`)
- **Copyright**: Copyright (C) 2009-2017 Tsuda Kageyu
- **License**: BSD 2-Clause — redistribution must retain the copyright notice and disclaimer,
  which are preserved in each source file header.

```text
BSD 2-Clause License

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

### HDE64 / Hacker Disassembler Engine (64-bit)

- **Source**: vendored copy under `src/EniBox.Loader/deps/MinHook/src/HDE/` (used by MinHook)
- **Copyright**: Copyright (c) 2008-2009, Vyacheslav Patkov
- **License**: BSD-style — the original copyright notice and license terms
  are preserved in each source file header (`hde64.c`, `hde64.h`, `table64.h`, `pstdint.h`).

---

## Third-party libraries via NuGet / 通过 NuGet 引用的库

| Package | Version | License | Notes |
|---|---|---|---|
| LZMA-SDK | 22.1.0 | Public Domain | LZMA compression; runtime decompression in the Loader is based on Igor Pavlov's public domain LZMA SDK code |
| CommunityToolkit.Mvvm | 8.3.2 | MIT | WPF MVVM toolkit |
| Microsoft.Extensions.DependencyInjection | 8.0.1 | MIT | Dependency injection |
| System.CommandLine | 2.0.0-beta4.22272.1 | MIT | CLI argument parsing |

NuGet packages are consumed as binary dependencies and are not redistributed in this repository.
