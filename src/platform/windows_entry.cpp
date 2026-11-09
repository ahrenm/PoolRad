// Windows GUI entry avoids allocating a console; CLI tools retain their streams.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
int poolrad_main(int,char**);
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {
    int count=0;auto** wide=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!wide)return 1;
    std::vector<std::string> values;values.reserve(static_cast<std::size_t>(count));
    for(int i=0;i<count;++i) {
        const auto size=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,nullptr,0,nullptr,nullptr);
        if(!size){LocalFree(wide);return 1;}
        std::string text(static_cast<std::size_t>(size),'\0');
        WideCharToMultiByte(CP_UTF8,0,wide[i],-1,text.data(),size,nullptr,nullptr);text.pop_back();values.push_back(std::move(text));
    }
    LocalFree(wide);std::vector<char*> args;bool unattended=false;
    for(auto& value:values) {
        args.push_back(value.data());
        if(value=="--smoke"||value=="--snapshot"||value=="--inspect"||value=="--extract"||value=="--vm-corpus"||value=="--ecl"||value=="--help")unattended=true;
    }
    args.push_back(nullptr);const auto result=poolrad_main(count,args.data());
    if(result && !unattended)MessageBoxW(nullptr,L"PoolRad could not start. See pr_setup.log in the installation folder for details.",L"PoolRad setup",MB_OK|MB_ICONERROR);
    return result;
}
#endif
