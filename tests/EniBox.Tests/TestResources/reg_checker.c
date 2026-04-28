#include <windows.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    HKEY hKey = NULL;
    LONG result;

    result = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\EniBoxTest", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        char value[256] = {0};
        DWORD size = sizeof(value);
        DWORD type = 0;
        result = RegQueryValueExA(hKey, "TestValue", NULL, &type, (LPBYTE)value, &size);
        if (result == ERROR_SUCCESS) {
            printf("CHECK:REG_READ:OK:Software\\EniBoxTest\\TestValue = %s\n", value);
        } else {
            printf("CHECK:REG_READ:FAIL:QueryValueEx error=%ld\n", result);
        }
        RegCloseKey(hKey);
    } else {
        printf("CHECK:REG_OPEN:FAIL:OpenKeyEx error=%ld\n", result);
    }

    result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        char productName[256] = {0};
        DWORD size = sizeof(productName);
        DWORD type = 0;
        result = RegQueryValueExA(hKey, "ProductName", NULL, &type, (LPBYTE)productName, &size);
        if (result == ERROR_SUCCESS) {
            printf("CHECK:REG_REAL:OK:ProductName = %s\n", productName);
        } else {
            printf("CHECK:REG_REAL:FAIL:error=%ld\n", result);
        }
        RegCloseKey(hKey);
    } else {
        printf("CHECK:REG_REAL:FAIL:OpenKeyEx error=%ld\n", result);
    }

    return 0;
}
