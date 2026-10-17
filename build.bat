@echo off
setlocal enabledelayedexpansion
rem Builds EnshroudedBaseCap.dll with llvm-mingw, no CMake needed.
rem
rem   build.bat          build build\EnshroudedBaseCap.dll
rem   build.bat test     build, then run the pacing test
rem   build.bat dist     build, then make dist\EnshroudedBaseCap-<version>.zip
rem
rem Toolchain: llvm-mingw (https://github.com/mstorsjo/llvm-mingw). Either put its
rem bin folder on PATH or set LLVM_MINGW to the unzipped folder.
rem With Visual Studio, use CMake instead (see README).

cd /d "%~dp0"
if defined LLVM_MINGW set "PATH=%LLVM_MINGW%\bin;%PATH%"
where x86_64-w64-mingw32-clang++ >nul 2>nul || (
  echo x86_64-w64-mingw32-clang++ not found.
  echo Download llvm-mingw ^(ucrt-x86_64 zip^) from https://github.com/mstorsjo/llvm-mingw/releases,
  echo unzip it, and set LLVM_MINGW to that folder or add its bin folder to PATH.
  exit /b 1
)

set "CC=x86_64-w64-mingw32-clang"
set "CXX=x86_64-w64-mingw32-clang++"
set "MH=third_party\minhook"
set "OUT=build"
if not exist %OUT% mkdir %OUT%

%CC% -O2 -c -I%MH%\include %MH%\src\hook.c -o %OUT%\hook.o || exit /b 1
%CC% -O2 -c -I%MH%\include %MH%\src\buffer.c -o %OUT%\buffer.o || exit /b 1
%CC% -O2 -c -I%MH%\include %MH%\src\trampoline.c -o %OUT%\trampoline.o || exit /b 1
%CC% -O2 -c -I%MH%\include %MH%\src\hde\hde64.c -o %OUT%\hde64.o || exit /b 1
x86_64-w64-mingw32-windres -Isrc src\basecap.rc -O coff -o %OUT%\basecap.res.o || exit /b 1
%CXX% -std=c++17 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -I%MH%\include -c src\basecap.cpp -o %OUT%\basecap.o || exit /b 1
%CXX% -shared -static -s -o %OUT%\EnshroudedBaseCap.dll %OUT%\basecap.o %OUT%\hook.o %OUT%\buffer.o %OUT%\trampoline.o %OUT%\hde64.o %OUT%\basecap.res.o -luser32 -lgdi32 || exit /b 1
echo Built %OUT%\EnshroudedBaseCap.dll

if /i "%~1"=="test" goto :test
if /i "%~1"=="dist" goto :dist
exit /b 0

:test
set "T=%OUT%\test"
if exist %T% rd /s /q %T%
mkdir %T%
%CC% -O2 -shared -o %T%\sl.interposer.dll tests\fake_interposer.c || exit /b 1
%CC% -O2 -o %T%\pacing_test.exe tests\pacing_test.c || exit /b 1
copy /y %OUT%\EnshroudedBaseCap.dll %T%\ >nul
%T%\pacing_test.exe || exit /b 1
exit /b 0

:dist
for /f "tokens=3" %%v in ('findstr /c:"define BASECAP_VERSION_STR" src\version.h') do set "VER=%%~v"
set "STAGE=%OUT%\dist"
if exist %STAGE% rd /s /q %STAGE%
mkdir %STAGE%\EnshroudedBaseCap-docs
copy /y %OUT%\EnshroudedBaseCap.dll %STAGE%\ >nul
copy /y package\EnshroudedBaseCap.ini %STAGE%\ >nul
copy /y package\install_basecap.bat %STAGE%\install_basecap.bat >nul
copy /y package\install_basecap.bat %STAGE%\uninstall_basecap.bat >nul
copy /y README.md %STAGE%\EnshroudedBaseCap-docs\ >nul
copy /y LICENSE %STAGE%\EnshroudedBaseCap-docs\LICENSE.txt >nul
copy /y NOTICE.md %STAGE%\EnshroudedBaseCap-docs\ >nul
copy /y CHANGELOG.md %STAGE%\EnshroudedBaseCap-docs\ >nul
copy /y third_party\minhook\LICENSE.txt %STAGE%\EnshroudedBaseCap-docs\MinHook-LICENSE.txt >nul
if not exist dist mkdir dist
set "ZIP=dist\EnshroudedBaseCap-v!VER!.zip"
if exist "!ZIP!" del "!ZIP!"
rem Windows' bsdtar writes standard zips; Compress-Archive in Windows PowerShell
rem stores folder paths with backslashes, which other unzip tools mangle.
"%SystemRoot%\System32\tar.exe" -a -c -f "!ZIP!" -C %STAGE% EnshroudedBaseCap.dll EnshroudedBaseCap.ini install_basecap.bat uninstall_basecap.bat EnshroudedBaseCap-docs || exit /b 1
echo Packaged !ZIP!
exit /b 0
