#include <windows.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    char childPath[MAX_PATH] = {0};
    if (argc >= 2) {
        strncpy(childPath, argv[1], MAX_PATH - 1);
    } else {
        printf("Usage: SubProcHost <child_exe_path>\n");
        return 4;
    }

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    BOOL created = CreateProcessA(childPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (!created) {
        printf("CHECK:SUBPROC_CREATE:FAIL:error=%lu\n", GetLastError());
        return 4;
    }

    printf("CHECK:SUBPROC_CREATE:OK:pid=%lu\n", pi.dwProcessId);

    WaitForSingleObject(pi.hProcess, 10000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    printf("CHECK:SUBPROC_EXIT:OK:code=%lu\n", exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}
