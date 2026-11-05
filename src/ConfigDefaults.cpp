#define WIN32_LEAN_AND_MEAN
#include "ConfigDefaults.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
constexpr DWORD maxConfigBytes=8*1024*1024;
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle):value(handle){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct TemporaryFile {
    std::wstring path;
    ~TemporaryFile(){if(!path.empty())DeleteFileW(path.c_str());}
};
struct CaseLess {
    bool operator()(const std::wstring& left,const std::wstring& right) const {return _wcsicmp(left.c_str(),right.c_str())<0;}
};
struct Line {std::size_t begin=0,end=0;std::wstring text;};
struct Entry {std::wstring key,block;};
struct Section {
    std::wstring name,block;
    std::size_t begin=0,end=0;
    std::set<std::wstring,CaseLess> keys;
    std::vector<Entry> entries;
};
enum class Encoding {Utf8,Utf8Bom,Utf16,Ansi};
std::wstring trim(const std::wstring& value) {
    const auto first=value.find_first_not_of(L" \t\r\n"),last=value.find_last_not_of(L" \t\r\n");
    return first==std::wstring::npos?L"":value.substr(first,last-first+1);
}
std::vector<Line> splitLines(const std::wstring& text) {
    std::vector<Line> result;
    for(std::size_t at=0;at<text.size();) {
        auto end=text.find(L'\n',at);if(end==std::wstring::npos)end=text.size();else ++end;
        auto contentEnd=end;
        while(contentEnd>at && (text[contentEnd-1]==L'\n' || text[contentEnd-1]==L'\r'))--contentEnd;
        result.push_back({at,end,text.substr(at,contentEnd-at)});at=end;
    }
    return result;
}
std::wstring sectionName(const std::wstring& text) {
    const auto line=trim(text);
    if(line.empty() || line.front()!=L'[')return L"";
    const auto end=line.find(L']');return end==std::wstring::npos?L"":trim(line.substr(1,end-1));
}
std::wstring keyName(const std::wstring& text) {
    const auto line=trim(text);
    if(line.empty() || line.front()==L';' || line.front()==L'#' || line.front()==L'[')return L"";
    const auto equal=line.find(L'=');return equal==std::wstring::npos?L"":trim(line.substr(0,equal));
}
std::vector<Section> sectionsIn(const std::wstring& text) {
    std::vector<Section> sections;std::wstring comments;
    for(const auto& line:splitLines(text)) {
        const auto name=sectionName(line.text);
        if(!name.empty()) {
            if(!sections.empty())sections.back().end=line.begin;
            Section section;section.name=name;section.begin=line.begin;section.end=text.size();
            sections.push_back(std::move(section));comments.clear();continue;
        }
        if(sections.empty())continue;
        const auto key=keyName(line.text);
        if(!key.empty()) {
            sections.back().keys.insert(key);
            sections.back().entries.push_back({key,comments+line.text+L"\n"});comments.clear();
        } else {
            const auto value=trim(line.text);
            if(!value.empty() && value.front()==L';')comments+=line.text+L"\n";else comments.clear();
        }
    }
    for(auto& section:sections)section.block=text.substr(section.begin,section.end-section.begin);
    return sections;
}
bool multibyte(const std::string& bytes,UINT codePage,std::wstring& text) {
    if(bytes.empty()){text.clear();return true;}
    const auto length=MultiByteToWideChar(codePage,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    if(!length)return false;
    text.resize(static_cast<std::size_t>(length));
    return MultiByteToWideChar(codePage,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),text.data(),length)==length;
}
bool decode(const std::string& bytes,std::wstring& text,Encoding& encoding) {
    if(bytes.compare(0,2,"\xff\xfe")==0) {
        if((bytes.size()-2)%sizeof(wchar_t))return false;
        encoding=Encoding::Utf16;text.resize((bytes.size()-2)/sizeof(wchar_t));
        if(!text.empty())std::memcpy(text.data(),bytes.data()+2,bytes.size()-2);
    } else if(bytes.compare(0,3,"\xef\xbb\xbf")==0) {
        encoding=Encoding::Utf8Bom;if(!multibyte(bytes.substr(3),CP_UTF8,text))return false;
    } else if(multibyte(bytes,CP_UTF8,text))encoding=Encoding::Utf8;
    else {encoding=Encoding::Ansi;if(!multibyte(bytes,CP_ACP,text))return false;}
    return text.find(L'\0')==std::wstring::npos;
}
bool encode(const std::wstring& text,Encoding encoding,std::string& bytes) {
    if(encoding==Encoding::Utf16) {
        bytes.assign("\xff\xfe",2);bytes.append(reinterpret_cast<const char*>(text.data()),text.size()*sizeof(wchar_t));return true;
    }
    if(text.empty()){bytes=encoding==Encoding::Utf8Bom?"\xef\xbb\xbf":"";return true;}
    const auto page=encoding==Encoding::Ansi?CP_ACP:CP_UTF8;
    const auto flags=encoding==Encoding::Ansi?WC_NO_BEST_FIT_CHARS:WC_ERR_INVALID_CHARS;
    BOOL substituted=FALSE;auto* used=encoding==Encoding::Ansi?&substituted:nullptr;
    const auto length=WideCharToMultiByte(page,flags,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,used);
    if(!length || substituted)return false;
    std::string content(static_cast<std::size_t>(length),'\0');
    if(WideCharToMultiByte(page,flags,text.data(),static_cast<int>(text.size()),content.data(),length,nullptr,used)!=length || substituted)return false;
    bytes=encoding==Encoding::Utf8Bom?"\xef\xbb\xbf":"";bytes+=content;return true;
}
DWORD readFile(const wchar_t* path,std::string& bytes,bool& exists) {
    Handle file(CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();exists=false;return error==ERROR_FILE_NOT_FOUND?ERROR_SUCCESS:error;
    }
    exists=true;LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.value,&size))return GetLastError();
    if(size.QuadPart<0 || size.QuadPart>maxConfigBytes)return ERROR_FILE_TOO_LARGE;
    bytes.resize(static_cast<std::size_t>(size.QuadPart));DWORD copied=0;
    if(!bytes.empty() && (!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&copied,nullptr) || copied!=bytes.size()))return ERROR_READ_FAULT;
    return ERROR_SUCCESS;
}
std::wstring blockFor(const std::wstring& block,const std::wstring& newline,bool ansi) {
    std::wstring result;
    for(const auto& line:splitLines(block)) {
        // 旧 ANSI 文件只补 ASCII 键值，避免新中文注释迫使原文件转换编码。
        const auto value=trim(line.text);if(ansi && !value.empty() && value.front()==L';')continue;
        result+=line.text+newline;
    }
    return result;
}
DWORD writeConfig(const wchar_t* path,const std::string& original,bool existed,const std::string& updated) {
    if(existed) {
        const auto attributes=GetFileAttributesW(path);
        if(attributes==INVALID_FILE_ATTRIBUTES)return GetLastError();
        if(attributes&FILE_ATTRIBUTE_READONLY)return ERROR_ACCESS_DENIED;
    }
    wchar_t absolute[MAX_PATH]{},temporary[MAX_PATH]{};wchar_t* filePart=nullptr;
    const auto length=GetFullPathNameW(path,MAX_PATH,absolute,&filePart);
    if(!length || length>=MAX_PATH || !filePart)return ERROR_BAD_PATHNAME;
    const std::wstring directory(absolute,filePart);
    if(!GetTempFileNameW(directory.c_str(),L"CGE",0,temporary))return GetLastError();
    TemporaryFile pending{temporary};
    {
        Handle file(CreateFileW(temporary,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(file.value==INVALID_HANDLE_VALUE)return GetLastError();
        DWORD written=0;
        if(!WriteFile(file.value,updated.data(),static_cast<DWORD>(updated.size()),&written,nullptr) || written!=updated.size())return ERROR_WRITE_FAULT;
        if(!FlushFileBuffers(file.value))return GetLastError();
    }
    std::string current;bool exists=false;const auto error=readFile(path,current,exists);
    if(error)return error;
    if(exists!=existed || current!=original)return ERROR_RETRY; // 不覆盖读取期间发生的外部修改。
    if(existed) {
        if(!ReplaceFileW(path,temporary,nullptr,0,nullptr,nullptr))return GetLastError();
    } else if(!MoveFileExW(temporary,path,MOVEFILE_WRITE_THROUGH))return GetLastError();
    pending.path.clear();WritePrivateProfileStringW(nullptr,nullptr,nullptr,path);return ERROR_SUCCESS;
}
}

ConfigDefaultsResult EnsureConfigDefaults(HMODULE module,const wchar_t* iniPath) {
    ConfigDefaultsResult result;
    try {
        if(!iniPath || !*iniPath){result.error=ERROR_INVALID_PARAMETER;return result;}
        const auto resource=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));
        const auto size=resource?SizeofResource(module,resource):0;
        const auto loaded=resource?LoadResource(module,resource):nullptr;
        const auto data=loaded?static_cast<const char*>(LockResource(loaded)):nullptr;
        if(!data || !size || size>maxConfigBytes){result.error=ERROR_RESOURCE_DATA_NOT_FOUND;return result;}
        const std::string defaults(data,size);std::wstring defaultText;Encoding defaultEncoding{};
        if(!decode(defaults,defaultText,defaultEncoding)){result.error=ERROR_NO_UNICODE_TRANSLATION;return result;}
        const auto schema=sectionsIn(defaultText);
        if(schema.empty()){result.error=ERROR_INVALID_DATA;return result;}
        std::string original;bool existed=false;
        result.error=readFile(iniPath,original,existed);if(result.error)return result;
        std::wstring text;Encoding encoding{};
        if(!decode(original,text,encoding)){result.error=ERROR_NO_UNICODE_TRANSLATION;return result;}
        std::string roundtrip;
        if(!encode(text,encoding,roundtrip) || roundtrip!=original){result.error=ERROR_NO_UNICODE_TRANSLATION;return result;}
        const auto sections=sectionsIn(text);
        const std::wstring newline=text.find(L"\r\n")!=std::wstring::npos?L"\r\n":text.find(L'\n')!=std::wstring::npos?L"\n":L"\r\n";
        std::map<std::size_t,std::wstring> insertions;std::wstring missingSections;
        // Win32 Profile API 不识别 UTF-8 BOM；单独放一行，保留 BOM 与原文本并使首分区可读取。
        const auto lines=splitLines(text);
        if(encoding==Encoding::Utf8Bom && !lines.empty() && !sectionName(lines.front().text).empty())insertions[0]+=newline;
        for(const auto& section:schema) {
            const auto found=std::find_if(sections.begin(),sections.end(),[&](const auto& candidate){return _wcsicmp(candidate.name.c_str(),section.name.c_str())==0;});
            if(found==sections.end()) {
                missingSections+=newline+blockFor(section.block,newline,encoding==Encoding::Ansi);
                result.addedKeys+=static_cast<unsigned>(section.entries.size());++result.addedSections;
            } else for(const auto& entry:section.entries)if(found->keys.find(entry.key)==found->keys.end()) {
                insertions[found->end]+=blockFor(entry.block,newline,encoding==Encoding::Ansi);++result.addedKeys;
            }
        }
        // 先补最后一个已有分区的键，再追加新分区，避免键被插入到新分区名下。
        if(!missingSections.empty())insertions[text.size()]+=missingSections;
        if(insertions.empty() && existed){result.success=true;return result;}
        for(auto it=insertions.rbegin();it!=insertions.rend();++it) {
            const auto prefix=it->first && text[it->first-1]!=L'\n' && text[it->first-1]!=L'\r'?newline:L"";
            text.insert(it->first,prefix+it->second);
        }
        std::string updated;
        if(!existed)updated=defaults;
        else if(!encode(text,encoding,updated)){result.error=ERROR_NO_UNICODE_TRANSLATION;return result;}
        result.error=writeConfig(iniPath,original,existed,updated);
        result.success=result.error==ERROR_SUCCESS;result.changed=result.success;return result;
    } catch(...) {result.error=ERROR_NOT_ENOUGH_MEMORY;return result;}
}
