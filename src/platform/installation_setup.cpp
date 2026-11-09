#include "gbengine/platform/native_installation.hpp"
#include <iostream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#endif
#endif

namespace GBEngine {
#ifdef _WIN32
namespace {
struct SetupWindow {HWND edit{},status{},accept{};bool requested=false,complete=false;};
std::wstring wide(std::string_view text) {
    const auto count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring value(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),value.data(),count);return value;
}
LRESULT CALLBACK setup_proc(HWND window,UINT message,WPARAM word,LPARAM parameter) {
    if(message==WM_NCCREATE)SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams));
    auto* state=reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(state && message==WM_COMMAND) {
        if(LOWORD(word)==IDOK){state->requested=true;return 0;}
        if(LOWORD(word)==IDCANCEL){DestroyWindow(window);return 0;}
    }
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    return DefWindowProcW(window,message,word,parameter);
}
}
#endif
std::optional<std::filesystem::path> run_installation_setup(const NativeInstallation& installation,std::ostream& log,
                            std::optional<std::filesystem::path> gog,bool hidden) {
#ifdef _WIN32
    const auto instance=GetModuleHandleW(nullptr);const wchar_t* name=L"PoolRadFirstRunSetup";
    WNDCLASSW type{};type.lpfnWndProc=setup_proc;type.hInstance=instance;type.lpszClassName=name;
    type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw DecodeError("Cannot open the setup window.");
    SetupWindow state;const auto style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;RECT bounds{0,0,640,310};AdjustWindowRect(&bounds,style,FALSE);
    const auto window=CreateWindowExW(WS_EX_CONTROLPARENT,name,L"PoolRad 0.1 beta - setup",style,
        (GetSystemMetrics(SM_CXSCREEN)-(bounds.right-bounds.left))/2,(GetSystemMetrics(SM_CYSCREEN)-(bounds.bottom-bounds.top))/2,
        bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,&state);
    if(!window)throw DecodeError("Cannot open the setup window.");
    const auto font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    const auto control=[&](const wchar_t* kind,const wchar_t* text,DWORD flags,int x,int y,int w,int h,int id,DWORD extra=0) {
        const auto handle=CreateWindowExW(extra,kind,text,WS_CHILD|WS_VISIBLE|flags,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
        SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return handle;
    };
    control(L"STATIC",L"Choose your Pool of Radiance DOS game folder",0,24,20,592,28,10);
    control(L"STATIC",L"Enter the path to POOLRAD. Spaces and Ctrl+V paste are supported.",0,24,54,592,26,11);
    state.edit=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,24,88,592,29,1000,WS_EX_CLIENTEDGE);
    SendMessageW(state.edit,EM_SETLIMITTEXT,32767,0);
    const auto destination=L"Install folder: "+installation.layout().root.wstring();
    control(L"STATIC",destination.c_str(),SS_PATHELLIPSIS,24,130,592,42,12);
    state.status=control(L"STATIC",L"Only game resources will be copied. Bundled saves and characters are omitted.",0,24,184,592,54,1001);
    state.accept=control(L"BUTTON",L"Set up",WS_TABSTOP|BS_DEFPUSHBUTTON,388,252,108,32,IDOK);
    const auto cancel=control(L"BUTTON",L"Cancel",WS_TABSTOP,508,252,108,32,IDCANCEL);
    ShowWindow(window,hidden?SW_HIDE:SW_SHOW);UpdateWindow(window);if(!hidden)SetFocus(state.edit);
    if(gog) {
        SetWindowTextW(state.edit,gog->c_str());SendMessageW(state.edit,EM_SETSEL,0,-1);
        SetWindowTextW(state.status,L"GOG installation found. Choose Set up, or enter another POOLRAD folder.");
    }
    std::optional<std::filesystem::path> selected;
    while(IsWindow(window)) {
        if(state.requested) {
            state.requested=false;EnableWindow(state.edit,FALSE);EnableWindow(state.accept,FALSE);EnableWindow(cancel,FALSE);
            try {
                const auto length=GetWindowTextLengthW(state.edit);std::wstring value(static_cast<std::size_t>(length)+1,L'\0');GetWindowTextW(state.edit,value.data(),length+1);value.resize(static_cast<std::size_t>(length));
                const auto source=setup_source_path(path_utf8(std::filesystem::path(value)));
                log<<"DOS source: "<<path_utf8(source)<<'\n';
                installation.import_from(source,[&](std::string_view text){log<<text<<'\n';SetWindowTextW(state.status,wide(text).c_str());UpdateWindow(window);});
                selected=source;
                state.complete=true;DestroyWindow(window);break;
            }catch(const std::exception& e){log<<"Setup: "<<e.what()<<'\n';SetWindowTextW(state.status,wide(e.what()).c_str());}
            EnableWindow(state.edit,TRUE);EnableWindow(state.accept,TRUE);EnableWindow(cancel,TRUE);SetFocus(state.edit);
        }
        MSG event{};
        if(!PeekMessageW(&event,nullptr,0,0,PM_REMOVE)){MsgWaitForMultipleObjects(0,nullptr,FALSE,100,QS_ALLINPUT);continue;}
        if(event.message==WM_QUIT){if(IsWindow(window))DestroyWindow(window);break;}
        if(!IsDialogMessageW(window,&event)){TranslateMessage(&event);DispatchMessageW(&event);}
    }
    DeleteObject(font);return selected;
#else
    (void)hidden;
    if(gog){try{installation.import_from(*gog);return gog;}catch(const std::exception& e){log<<e.what()<<'\n';}}
    // Console line input also preserves pasted spaces on non-Windows hosts.
    for(std::string line;;) {
        std::cout<<"PoolRad setup: enter the POOLRAD folder path (blank to cancel): "<<std::flush;
        if(!std::getline(std::cin,line) || line.empty())return {};
        try{const auto source=setup_source_path(line);installation.import_from(source);return source;}catch(const std::exception& e){log<<e.what()<<'\n';}
    }
#endif
}
}
