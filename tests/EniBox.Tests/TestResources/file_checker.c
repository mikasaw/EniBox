#include <windows.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: FileChecker <file1> [file2] ...\n");
        return 0;
    }

    int all_ok = 1;
    for (int i = 1; i < argc; i++) {
        const char* path = argv[i];
        HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            char buf[256] = {0};
            DWORD read = 0;
            ReadFile(hFile, buf, sizeof(buf)-1, &read, NULL);
            CloseHandle(hFile);
            printf("CHECK:FILE_OPEN:OK:%s\n", path);
            if (read > 0) {
                printf("CHECK:FILE_READ:OK:%s (%lu bytes)\n", path, read);
            } else {
                printf("CHECK:FILE_READ:FAIL:%s (0 bytes)\n", path);
                all_ok = 0;
            }
        } else {
            DWORD err = GetLastError();
            printf("CHECK:FILE_OPEN:FAIL:%s (error=%lu)\n", path, err);
            all_ok = 0;
        }
    }
    return all_ok ? 0 : 1;
}
