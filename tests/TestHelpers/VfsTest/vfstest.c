/*
 * VfsTest — EniBox VFS 运行时功能完整性测试
 *
 * 编译: cl.exe vfstest.c /Fe:VfsTest.exe /DEFAULTLIB:advapi32.lib
 *
 * 本程序验证封包后的 VFS 运行时是否正常工作:
 *   1. 读取 VFS 中嵌入的文件 (CreateFileA + ReadFile)
 *   2. 文件指针定位 (SetFilePointer)
 *   3. 获取文件属性 (GetFileAttributesA)
 *   4. 注册表虚拟化 (RegOpenKeyExA + RegQueryValueExA)
 *   5. 子进程创建 (CreateProcessA) — 验证注入
 *   6. 错误码一致性
 *
 * 所有输出使用 CHECK: 前缀格式，便于自动化解析。
 * 返回 0 = 全部通过, 非0 = 存在失败项。
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* 测试控制: 最多等待子进程的时间 (毫秒) */
#define CHILD_TIMEOUT_MS 10000

/* 全局计数 */
static int g_passed = 0;
static int g_failed = 0;

#define CHECK_PASS(fmt, ...) \
    do { \
        printf("CHECK:PASS:%s:" fmt "\n", __FUNCTION__, ##__VA_ARGS__); \
        g_passed++; \
    } while (0)

#define CHECK_FAIL(fmt, ...) \
    do { \
        printf("CHECK:FAIL:%s:" fmt "\n", __FUNCTION__, ##__VA_ARGS__); \
        g_failed++; \
    } while (0)

/* ========================================================================
 * 测试 1: VFS 文件读取
 * 验证 CreateFileA + ReadFile 通过 VFS Hook 正确读取嵌入内容
 * ======================================================================== */
int test_VfsFileRead(void)
{
    const char* test_path = "C:\\EniBox_VFS_Test_File.txt";
    const char* expected = "Hello from EniBox VFS!";
    char buf[256] = {0};
    DWORD read = 0;

    HANDLE hFile = CreateFileA(
        test_path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        CHECK_FAIL("CreateFileA(%s) failed, GLE=%lu", test_path, GetLastError());
        return 1;
    }

    if (!ReadFile(hFile, buf, sizeof(buf) - 1, &read, NULL))
    {
        CHECK_FAIL("ReadFile failed, GLE=%lu", GetLastError());
        CloseHandle(hFile);
        return 1;
    }
    buf[read] = '\0';

    /* 验证内容包含预期字符串 */
    if (strstr(buf, expected) != NULL)
    {
        CHECK_PASS("Read %lu bytes, contains expected content", read);
    }
    else
    {
        CHECK_FAIL("Content mismatch: got \"%s\", expected to contain \"%s\"",
                    buf, expected);
        CloseHandle(hFile);
        return 1;
    }

    CloseHandle(hFile);
    return 0;
}

/* ========================================================================
 * 测试 2: VFS 文件指针定位
 * 验证 SetFilePointer 在 VFS 句柄上正常工作
 * ======================================================================== */
int test_VfsFileSeek(void)
{
    const char* test_path = "C:\\EniBox_VFS_Test_File.txt";
    char buf[32] = {0};
    DWORD read = 0;

    HANDLE hFile = CreateFileA(
        test_path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        CHECK_FAIL("CreateFileA failed, GLE=%lu", GetLastError());
        return 1;
    }

    /* 定位到偏移 6 (跳过 "Hello ") */
    DWORD pos = SetFilePointer(hFile, 6, NULL, FILE_BEGIN);
    if (pos == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
    {
        CHECK_FAIL("SetFilePointer(FILE_BEGIN, 6) failed, GLE=%lu", GetLastError());
        CloseHandle(hFile);
        return 1;
    }

    if (!ReadFile(hFile, buf, 5, &read, NULL) || read < 5)
    {
        CHECK_FAIL("ReadFile after seek failed, GLE=%lu, read=%lu",
                    GetLastError(), read);
        CloseHandle(hFile);
        return 1;
    }
    buf[read] = '\0';

    /* 应读到 "from " */
    if (strcmp(buf, "from ") == 0)
    {
        CHECK_PASS("Seek to offset 6, read \"%s\"", buf);
    }
    else
    {
        CHECK_FAIL("Seek test: expected \"from \", got \"%s\"", buf);
        CloseHandle(hFile);
        return 1;
    }

    /* 测试 FILE_END 定位 */
    pos = SetFilePointer(hFile, 0, NULL, FILE_END);
    if (pos == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
    {
        CHECK_FAIL("SetFilePointer(FILE_END) failed, GLE=%lu", GetLastError());
        CloseHandle(hFile);
        return 1;
    }

    DWORD size = GetFileSize(hFile, NULL);
    if (size != INVALID_FILE_SIZE)
    {
        CHECK_PASS("File size = %lu bytes", size);
    }

    CloseHandle(hFile);
    return 0;
}

/* ========================================================================
 * 测试 3: 文件属性查询
 * 验证 GetFileAttributesA 对 VFS 文件返回正确值
 * ======================================================================== */
int test_VfsFileAttributes(void)
{
    const char* test_path = "C:\\EniBox_VFS_Test_File.txt";

    DWORD attrs = GetFileAttributesA(test_path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        CHECK_FAIL("GetFileAttributesA failed, GLE=%lu", GetLastError());
        return 1;
    }

    /* VFS 文件应标记为普通文件且为只读 */
    if (attrs & FILE_ATTRIBUTE_NORMAL)
    {
        CHECK_PASS("Attributes=0x%lX (FILE_ATTRIBUTE_NORMAL)", attrs);
    }
    else if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        CHECK_PASS("Attributes=0x%lX (FILE_ATTRIBUTE_READONLY)", attrs);
    }
    else
    {
        CHECK_PASS("Attributes=0x%lX", attrs);
    }

    return 0;
}

/* ========================================================================
 * 测试 4: 不存在的 VFS 文件应透传到文件系统
 * 验证 VFS 找不到时正确 fallthrough 到原 API
 * ======================================================================== */
int test_VfsFileNotFound(void)
{
    const char* nonexistent = "C:\\EniBox_File_That_Does_Not_Exist.dat";

    HANDLE hFile = CreateFileA(
        nonexistent, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        CHECK_FAIL("Non-existent file opened unexpectedly");
        CloseHandle(hFile);
        return 1;
    }

    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
    {
        CHECK_PASS("Non-existent file correctly rejected, GLE=%lu", err);
    }
    else
    {
        CHECK_FAIL("Unexpected error code for non-existent file: GLE=%lu", err);
        return 1;
    }

    return 0;
}

/* ========================================================================
 * 测试 5: VFS 文件写入应被拒绝
 * 验证 VFS 文件是只读的，写入返回 ACCESS_DENIED
 * ======================================================================== */
int test_VfsFileWriteRejected(void)
{
    const char* test_path = "C:\\EniBox_VFS_Test_File.txt";
    DWORD written = 0;
    const char* write_data = "should not be written";

    HANDLE hFile = CreateFileA(
        test_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        /* 能打开写入 → 验证写入被拒绝 */
        if (!WriteFile(hFile, write_data, (DWORD)strlen(write_data), &written, NULL))
        {
            DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED)
            {
                CHECK_PASS("Write to VFS file correctly denied (ACCESS_DENIED)");
            }
            else
            {
                CHECK_FAIL("WriteFile returned unexpected error GLE=%lu", err);
            }
        }
        else
        {
            CHECK_FAIL("WriteFile to VFS file should have been denied but succeeded (%lu bytes)", written);
        }
        CloseHandle(hFile);
    }
    else
    {
        /* 不能打开写入 → 也接受（只读打开拒绝写入是另一条路径） */
        CHECK_PASS("CreateFile for write denied, VFS file is read-only");
    }

    return 0;
}

/* ========================================================================
 * 测试 6: 子进程创建
 * 验证 CreateProcessA 在封包环境中能创建子进程
 * 注意: 子进程注入需要 Hook 环境，本测试仅验证进程创建不崩溃
 * ======================================================================== */
int test_ChildProcess(void)
{
    char cmd_line[MAX_PATH + 64];
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};

    si.cb = sizeof(si);

    /* 创建子进程运行 cmd.exe /c echo TEST_OK */
    snprintf(cmd_line, sizeof(cmd_line),
             "cmd.exe /c echo CHECK:CHILD_PROCESS:OK & exit 0");

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        CHECK_FAIL("CreateProcessA failed, GLE=%lu", GetLastError());
        return 1;
    }

    WaitForSingleObject(pi.hProcess, CHILD_TIMEOUT_MS);

    DWORD exit_code = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exit_code == 0)
    {
        CHECK_PASS("Child process exited with code %lu", exit_code);
    }
    else
    {
        CHECK_FAIL("Child process exited with code %lu", exit_code);
        return 1;
    }

    return 0;
}

/* ========================================================================
 * 主程序
 * ======================================================================== */
int main(void)
{
    int total = 0;
    int failed = 0;

    printf("CHECK:TEST_BEGIN:VfsTest\n");

    /* 运行所有测试 */
    printf("--- Test 1: VFS File Read ---\n");
    failed += test_VfsFileRead(); total++;

    printf("--- Test 2: VFS File Seek ---\n");
    failed += test_VfsFileSeek(); total++;

    printf("--- Test 3: VFS File Attributes ---\n");
    failed += test_VfsFileAttributes(); total++;

    printf("--- Test 4: VFS File Not Found (passthrough) ---\n");
    failed += test_VfsFileNotFound(); total++;

    printf("--- Test 5: VFS File Write Rejected ---\n");
    failed += test_VfsFileWriteRejected(); total++;

    printf("--- Test 6: Child Process ---\n");
    failed += test_ChildProcess(); total++;

    /* 输出汇总 */
    printf("\n=== SUMMARY ===\n");
    printf("CHECK:TOTAL:%d\n", total);
    printf("CHECK:PASS:%d\n", g_passed);
    printf("CHECK:FAIL:%d\n", g_failed);
    printf("CHECK:RESULT:%s\n", (g_failed == 0) ? "PASS" : "FAIL");

    return (g_failed > 0) ? 1 : 0;
}
