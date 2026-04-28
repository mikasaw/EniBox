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
