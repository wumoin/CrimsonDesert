// 仅操作独立临时目录中的 INI，不加载或操作游戏。
#define WIN32_LEAN_AND_MEAN
#include "../src/ConfigDefaults.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
void check(bool value,const char* expression,int line) {
    if(!value){std::fprintf(stderr,"FAIL line %d: %s\n",line,expression);std::exit(1);}
}
#define CHECK(value) check(static_cast<bool>(value),#value,__LINE__)
std::wstring directory;
std::vector<std::wstring> files;
std::wstring path(const wchar_t* name) {auto value=directory+L"\\"+name;files.push_back(value);return value;}
void write(const std::wstring& file,const std::string& bytes) {
    FILE* stream=nullptr;CHECK(!_wfopen_s(&stream,file.c_str(),L"wb") && stream);
    CHECK(fwrite(bytes.data(),1,bytes.size(),stream)==bytes.size());CHECK(!fclose(stream));
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,file.c_str());
}
std::string read(const std::wstring& file) {
    FILE* stream=nullptr;CHECK(!_wfopen_s(&stream,file.c_str(),L"rb") && stream);
    CHECK(!_fseeki64(stream,0,SEEK_END));const auto length=_ftelli64(stream);CHECK(length>=0);CHECK(!_fseeki64(stream,0,SEEK_SET));
    std::string bytes(static_cast<std::size_t>(length),'\0');CHECK(fread(bytes.data(),1,bytes.size(),stream)==bytes.size());CHECK(!fclose(stream));return bytes;
}
std::wstring setting(const std::wstring& file,const wchar_t* section,const wchar_t* key) {
    wchar_t value[512]{};GetPrivateProfileStringW(section,key,L"<missing>",value,512,file.c_str());return value;
}
std::string utf16(const std::wstring& text) {std::string bytes("\xff\xfe",2);bytes.append(reinterpret_cast<const char*>(text.data()),text.size()*sizeof(wchar_t));return bytes;}
bool subsequence(const std::string& original,const std::string& updated) {
    std::size_t at=0;for(const auto byte:updated)if(at<original.size() && byte==original[at])++at;return at==original.size();
}
ConfigDefaultsResult complete(const std::wstring& file) {return EnsureConfigDefaults(GetModuleHandleW(nullptr),file.c_str());}
void assertIdempotent(const std::wstring& file) {
    const auto bytes=read(file);const auto result=complete(file);
    if(!result.success || result.changed)std::fwprintf(stderr,L"Non-idempotent file %ls: success=%d changed=%d keys=%u sections=%u error=%lu\n",file.c_str(),result.success,result.changed,result.addedKeys,result.addedSections,result.error);
    CHECK(result.success && !result.changed && !result.error && !result.addedKeys && !result.addedSections);CHECK(read(file)==bytes);
}
void testLegacyUtf8() {
    const auto file=path(L"legacy.ini");
    const std::string before=u8"; 用户注释：保留中文与符号 = ;\r\n[Experiment]\r\nEnabled=0\r\n[Conditions]\r\nIgnoreTribe=0\r\n"
        "[Relationships]\r\nPreferredPartnerKey=778\r\nBeautyPartnerPicker=0\r\n[UI]\r\nLanguage=en-US\r\n"
        "[PartnerNames]\r\n3.778.2211=v1:5C0F7F8E\r\n[MySettings]\r\nMystery = keep ; exactly";
    write(file,before);const auto result=complete(file);
    CHECK(result.success && result.changed && result.addedKeys>0 && result.addedSections==0);
    const auto updated=read(file);CHECK(subsequence(before,updated));
    CHECK(updated.find(u8"; 管理窗口热键")!=std::string::npos);
    CHECK(updated.find("Mystery = keep ; exactly")!=std::string::npos);
    CHECK(setting(file,L"Experiment",L"Enabled")==L"0");CHECK(setting(file,L"Conditions",L"IgnoreTribe")==L"0");
    CHECK(setting(file,L"Conditions",L"IgnoreAffinityLevel")==L"1");
    CHECK(setting(file,L"Relationships",L"PreferredPartnerKey")==L"778");CHECK(setting(file,L"Relationships",L"BeautyPartnerPicker")==L"0");
    CHECK(setting(file,L"UI",L"Language")==L"en-US");CHECK(setting(file,L"UI",L"ManagerHotkey")==L"F8");
    CHECK(setting(file,L"PartnerNames",L"3.778.2211")==L"v1:5C0F7F8E");assertIdempotent(file);
    puts("PASS: old UTF-8 INI gains documented missing keys; explicit values, comments, names and unknown sections preserved.");
}
void testMissingAndComplete() {
    const auto file=path(L"new.ini");const auto result=complete(file);CHECK(result.success && result.changed && result.addedKeys>0 && result.addedSections>=5);
    CHECK(setting(file,L"Experiment",L"Enabled")==L"1");CHECK(setting(file,L"UI",L"ManagerHotkey")==L"F8");
    CHECK(setting(file,L"UI",L"Language")==L"zh-CN");CHECK(read(file).find("[PartnerNames]")!=std::string::npos);
    const auto source=FindResourceW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));CHECK(source);
    const auto data=static_cast<const char*>(LockResource(LoadResource(GetModuleHandleW(nullptr),source)));CHECK(data);
    CHECK(read(file)==std::string(data,SizeofResource(GetModuleHandleW(nullptr),source)));
    assertIdempotent(file);CHECK(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY));assertIdempotent(file);
    CHECK(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL));
    const auto empty=path(L"empty.ini");write(empty,"");CHECK(complete(empty).success);CHECK(setting(empty,L"UI",L"ManagerHotkey")==L"F8");assertIdempotent(empty);
    puts("PASS: missing/empty INI generated from embedded shipping defaults; complete and read-only complete files are not rewritten.");
}
void testUnicodeAndCase() {
    const auto file=path(L"unicode.ini");
    const auto before=utf16(L"; 自定义注释\n[experiment]\nenabled=bad-value\n[ UI ] ; comment\nmanagerhotkey=\nlanguage=en-US\n[PartnerNames]\n3.99.100=v1:4E2D6587");
    write(file,before);const auto result=complete(file);CHECK(result.success && result.changed);
    const auto updated=read(file);CHECK(updated.compare(0,2,"\xff\xfe")==0 && subsequence(before,updated));
    CHECK(setting(file,L"Experiment",L"Enabled")==L"bad-value");CHECK(setting(file,L"UI",L"ManagerHotkey").empty());
    CHECK(setting(file,L"UI",L"Language")==L"en-US");CHECK(setting(file,L"PartnerNames",L"3.99.100")==L"v1:4E2D6587");assertIdempotent(file);
    const auto bom=path(L"bom.ini");const std::string original="\xef\xbb\xbf[UI]\r\nManagerHotkey=Alt+M\r\n";
    write(bom,original);CHECK(complete(bom).success);CHECK(read(bom).compare(0,3,"\xef\xbb\xbf")==0 && subsequence(original,read(bom)));
    CHECK(setting(bom,L"UI",L"ManagerHotkey")==L"Alt+M");assertIdempotent(bom);
    puts("PASS: UTF-16LE/UTF-8 BOM, case/spacing, missing sections and existing empty/invalid values preserved.");
}
void testAnsiAndDuplicates() {
    const auto file=path(L"ansi.ini");const wchar_t* comment=L"; 中文注释 café\r\n[UI]\r\nManagerHotkey=Ctrl+F9\r\n";
    const auto length=WideCharToMultiByte(CP_ACP,0,comment,-1,nullptr,0,nullptr,nullptr);CHECK(length>0);
    std::string original(static_cast<std::size_t>(length),'\0');CHECK(WideCharToMultiByte(CP_ACP,0,comment,-1,original.data(),length,nullptr,nullptr)==length);original.pop_back();
    write(file,original);CHECK(complete(file).success);CHECK(subsequence(original,read(file)));CHECK(setting(file,L"UI",L"ManagerHotkey")==L"Ctrl+F9");assertIdempotent(file);
    const auto duplicate=path(L"duplicate.ini");const std::string before="[UI]\nLanguage=en-US\n[ui]\nManagerHotkey=Alt+M\n[Unknown]\nName=untouched";
    write(duplicate,before);CHECK(complete(duplicate).success);CHECK(subsequence(before,read(duplicate)));
    CHECK(setting(duplicate,L"UI",L"ManagerHotkey")==L"F8");CHECK(read(duplicate).find("ManagerHotkey=Alt+M")!=std::string::npos);assertIdempotent(duplicate);
    puts("PASS: legacy ANSI bytes and duplicate sections preserved; missing keys added to the native first section.");
}
void testFailures() {
    const auto readonly=path(L"readonly.ini");const std::string before="[Experiment]\r\nEnabled=0\r\n";write(readonly,before);
    CHECK(SetFileAttributesW(readonly.c_str(),FILE_ATTRIBUTE_READONLY));const auto denied=complete(readonly);
    CHECK(!denied.success && !denied.changed && denied.error==ERROR_ACCESS_DENIED && read(readonly)==before);
    CHECK(SetFileAttributesW(readonly.c_str(),FILE_ATTRIBUTE_NORMAL));
    const auto locked=path(L"locked.ini");write(locked,before);
    const auto lock=CreateFileW(locked.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(lock!=INVALID_HANDLE_VALUE);
    const auto blocked=complete(locked);CHECK(!blocked.success && !blocked.changed && read(locked)==before);CHECK(CloseHandle(lock));
    const auto binary=path(L"binary.ini");const std::string invalid("[UI]\0garbage",12);write(binary,invalid);
    const auto rejected=complete(binary);CHECK(!rejected.success && !rejected.changed && read(binary)==invalid);
    WIN32_FIND_DATAW found{};const auto leftovers=FindFirstFileW((directory+L"\\CGE*.tmp").c_str(),&found);CHECK(leftovers==INVALID_HANDLE_VALUE);
    puts("PASS: read-only, replacement sharing failure and binary data leave originals unchanged; temporary files cleaned up.");
}
}
int wmain() {
    wchar_t base[MAX_PATH]{},folder[MAX_PATH]{};CHECK(GetTempPathW(MAX_PATH,base));CHECK(GetTempFileNameW(base,L"CGT",0,folder));
    CHECK(DeleteFileW(folder));CHECK(CreateDirectoryW(folder,nullptr));directory=folder;
    testLegacyUtf8();testMissingAndComplete();testUnicodeAndCase();testAnsiAndDuplicates();testFailures();
    for(const auto& file:files)CHECK(DeleteFileW(file.c_str()));CHECK(RemoveDirectoryW(directory.c_str()));
    puts("All config defaults tests passed.");return 0;
}
