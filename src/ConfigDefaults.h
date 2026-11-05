#pragma once
#include <windows.h>

struct ConfigDefaultsResult {
    bool success=false,changed=false;
    unsigned addedKeys=0,addedSections=0;
    DWORD error=ERROR_SUCCESS;
};
// 从插件内置的默认 INI 补齐缺失项，已有值、注释、未知分区和名字保持原样。
ConfigDefaultsResult EnsureConfigDefaults(HMODULE module,const wchar_t* iniPath);
