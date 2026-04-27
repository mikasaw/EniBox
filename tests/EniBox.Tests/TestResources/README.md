# 测试目标程序清单

本目录存放测试用的目标程序文件。

## 系统程序路径

测试将使用以下系统内置程序：

### Windows系统程序
- **fc.exe**: 文件比较工具（C:\Windows\System32\fc.exe）
- **notepad.exe**: 记事本（C:\Windows\System32\notepad.exe）
- **calc.exe**: 计算器（C:\Windows\System32\calc.exe）

### 测试用途
1. **fc.exe**: 用于端到端封包测试（已在IntegrationTests中使用）
2. **notepad.exe**: 用于GUI封包流程测试
3. **calc.exe**: 用于子进程注入测试

## 注意事项
- 系统程序路径在测试代码中动态获取
- 不需要将系统程序复制到本目录
- 本目录仅用于存放自定义测试程序（如有需要）
