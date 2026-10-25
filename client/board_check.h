#pragma once
static int CheckBoard(void)
{
    HKEY key;
    wchar_t product[128];
    DWORD type = 0, size = sizeof(product);
    LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &key);
    if (st != ERROR_SUCCESS) return 0;
    st = RegQueryValueExW(key, L"SystemProductName", NULL, &type,
                         (BYTE*)product, &size);
    RegCloseKey(key);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return 0;
    product[(sizeof(product) / sizeof(product[0])) - 1] = L'\0';
    if (wcsstr(product, L"GA401IU") == NULL) {
        fwprintf(stderr, L"Refusing unsupported product: %ls\n", product);
        return 0;
    }
    return 1;
}

