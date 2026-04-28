#include <windows.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    const char* testFile = "test_data.txt";
    if (argc >= 2) {
        testFile = argv[1];
    }

    HANDLE hFile = CreateFileA(testFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char buf[256] = {0};
        DWORD read = 0;
        ReadFile(hFile, buf, sizeof(buf)-1, &read, NULL);
        CloseHandle(hFile);
        printf("CHECK:CHILD_VFS:OK:%s (%lu bytes)\n", testFile, read);
        return 0;
    } else {
        printf("CHECK:CHILD_VFS:FAIL:error=%lu\n", GetLastError());
        return 1;
    }
}
