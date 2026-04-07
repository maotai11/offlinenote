// test/test_winmain_stub.cpp
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

extern int main(int, char**);

static int convert_and_call() {
    // Get the command line as wide string
    LPCWSTR wcmd = GetCommandLineW();
    
    // Parse into arguments (respecting quotes)
    std::vector<std::string> args;
    std::wstring warg;
    bool inQuotes = false;
    bool escaping = false;
    
    for (size_t i = 0; wcmd[i]; i++) {
        wchar_t c = wcmd[i];
        if (escaping) {
            warg += c;
            escaping = false;
        } else if (c == L'^') {
            escaping = true;
        } else if (c == L'"') {
            inQuotes = !inQuotes;
            warg += c;
        } else if ((c == L' ' || c == L'\t') && !inQuotes) {
            if (!warg.empty()) {
                int len = WideCharToMultiByte(CP_UTF8, 0, warg.c_str(), -1, NULL, 0, NULL, NULL);
                std::string a(len, 0);
                WideCharToMultiByte(CP_UTF8, 0, warg.c_str(), -1, &a[0], len, NULL, NULL);
                if (!a.empty() && a.back() == 0) a.pop_back();
                args.push_back(a);
                warg.clear();
            }
        } else {
            warg += c;
        }
    }
    if (!warg.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, warg.c_str(), -1, NULL, 0, NULL, NULL);
        std::string a(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, warg.c_str(), -1, &a[0], len, NULL, NULL);
        if (!a.empty() && a.back() == 0) a.pop_back();
        args.push_back(a);
    }
    
    if (args.empty()) {
        args.push_back("test");
    }
    
    // Build argv array
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    
    return main((int)args.size(), argv.data());
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return convert_and_call();
}
#endif
