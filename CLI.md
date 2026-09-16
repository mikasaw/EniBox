# CLI 使用文档

EniBox 支持命令行模式进行自动化封包操作。

## 基本用法

```bash
EniBox.exe --cli --source <源EXE路径> --output <输出路径> [选项]
```

## 参数说明

### 必需参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `--source` | 源 EXE 文件路径 | `--source C:\app\myapp.exe` |
| `--output` | 输出文件路径 | `--output C:\release\myapp.enibox` |

### 可选参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--files` | (空) | 依赖文件列表，用分号分隔 |
| `--dirs` | (空) | 依赖目录列表，用分号分隔，目录下所有文件将被包含 |
| `--compress` | `true` | 启用 LZMA 压缩 |
| `--registry-virtualization` | `false` | 启用注册表虚拟化 |
| `--subprocess-injection` | `true` | 启用子进程注入 |

### 帮助参数

| 参数 | 说明 |
|------|------|
| `--help` / `-h` / `-?` | 显示帮助信息 |

## 参数行为细节

### `--files` / `--dirs`

- 多个路径用分号 `;` 分隔；**不存在的路径会报错并以退出码 1 终止**（不会静默跳过产生残缺包）。
- 依赖文件的虚拟路径 = 相对于源 EXE 所在目录的相对路径（绝对路径源则保留绝对路径）。
- CLI 暂不支持自定义虚拟路径映射；需要任意映射请使用编程 API
  （`PackFileItem.VirtualPath`）或 GUI。

### `--compress`

- 接受 `on|off|true|false`（大小写不敏感，亦接受 `yes|no|1|0`），默认 `on`。
- 注意：小数据可能压缩后反而更大，打包器会自动回退为原样存储
  （值数据不变，仅标志位不同）。

### `--registry-virtualization`

- 语义：预置键路径即作用域根，整个子树（含运行时新建键）全部虚拟化；
  写入/删除不触碰真实注册表，并持久化到产物同目录 `<产物>.vreg.bin`
  （启动时存在则整体替换预置值；删除该文件即重置为预置值，
  被删除的预置值会回来）；作用域外的键完全透传。
  多实例并发为后写赢无锁，建议单实例运行。
- 删除语义：`RegDeleteValue`/`RegDeleteKey`/`RegDeleteTree`/`RegDeleteKeyEx`
  均已挂钩（删键有子键时返回 ACCESS_DENIED；`RegDeleteTree` 删子键与值、
  键本身保留，与真实注册表一致；删除作用域根被拒绝——会使子树退出
  虚拟化）。

### `--registry-value`

- 预置注册表值，可重复传入。格式：`KEY|NAME|TYPE|DATA` 或
  `KEY|NAME|DATA`（TYPE 缺省 `SZ`）。
- `TYPE ∈ {SZ, EXPAND_SZ, DWORD, BINARY}`；DWORD 为无符号十进制，
  BINARY 为十六进制串（`DE AD BE EF` 或 `DEADBEEF`）；
  `MULTI_SZ` 仅编程 API（`PackRegistryValue`）支持。
- 限制：字段以 `|` 分隔，SZ/EXPAND_SZ 文本中无法包含 `|` 字符
  （需要时请改用编程 API）。
- 示例：

```bash
EniBox.exe --cli --source app.exe --output app.enibox \
  --registry-virtualization \
  --registry-value "HKEY_CURRENT_USER\Software\MyApp|InstallDir|SZ|C:\\Apps\\MyApp" \
  --registry-value "HKEY_CURRENT_USER\Software\MyApp|LaunchCount|DWORD|0"
```

### 退出码契约

| 退出码 | 含义 |
|--------|------|
| `0` | 封包成功 |
| `1` | 参数/用法错误或封包失败（stderr 给出原因） |
| `2` | 用户取消（Ctrl+C） |

### Loader 来源与指纹

封包时按 `Resources\EniBox.Loader.<arch>.dll` → 同目录 → 通用名顺序
优先取磁盘 Loader，回退嵌入资源；进度输出会打印所用 Loader 的
SHA256 前 16 位与来源路径，便于核对版本。

## 使用示例

### 基本封包

```bash
EniBox.exe --cli --source myapp.exe --output myapp.enibox
```

### 包含依赖文件

```bash
EniBox.exe --cli --source myapp.exe --output myapp.enibox --files "lib1.dll;lib2.dll;data.bin"
```

### 包含依赖目录

```bash
EniBox.exe --cli --source myapp.exe --output myapp.enibox --dirs "C:\libs;C:\data"
```

### 启用所有高级特性

```bash
EniBox.exe --cli --source myapp.exe --output myapp.enibox --registry-virtualization --subprocess-injection
```

### 禁用压缩

```bash
EniBox.exe --cli --source myapp.exe --output myapp.enibox --compress false
```

## 退出码

| 退出码 | 说明 |
|--------|------|
| 0 | 成功 |
| 1 | 参数错误或封包失败 |
| 2 | 用户取消 (Ctrl+C) |
| 3 | 运行时异常 |

## 输出格式

CLI 模式会在控制台输出封包进度：

```
[CollectingFiles] 0.0% - Validating input...
[BuildingVFS] 50.0% - Building VFS...
[Compressing] 75.0% - Compression complete.
[ModifyingPE] 90.0% - Modifying PE structure...
[WritingOutput] 100.0% - Writing output...
Success: C:\release\myapp.enibox (2048576 bytes)
```

## GUI 模式

不带 `--cli` 参数启动时进入 GUI 模式：

```bash
EniBox.exe
```

## 注意事项

- `--source` 指定的文件必须是有效的 PE (EXE) 文件
- `--files` 中的依赖文件必须存在，否则返回错误
- `--dirs` 中的目录必须存在，不存在的目录会被跳过
- 输出文件路径的父目录必须可写
- 注册表虚拟化默认关闭，需显式启用
