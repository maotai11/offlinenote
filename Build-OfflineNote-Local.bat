@echo off
setlocal

set "REPO_DIR=%~dp0"
set "MSYS2_ROOT=C:\msys64\mingw64"
set "MSYS2_BIN=%MSYS2_ROOT%\bin"

if not exist "%MSYS2_BIN%\c++.exe" (
    echo [ERROR] MinGW64 compiler not found: "%MSYS2_BIN%\c++.exe"
    echo [HINT] Install or restore MSYS2 MinGW64 under C:\msys64\mingw64
    exit /b 1
)

set "PATH=%MSYS2_BIN%;%PATH%"

cmake -B "%REPO_DIR%build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %ERRORLEVEL%

cmake --build "%REPO_DIR%build" --parallel
if errorlevel 1 exit /b %ERRORLEVEL%

ctest --test-dir "%REPO_DIR%build" --output-on-failure
exit /b %ERRORLEVEL%
