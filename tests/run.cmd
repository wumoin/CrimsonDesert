@echo off
setlocal
pushd "%~dp0.."
if not "%~2"=="" goto usage
where cl.exe >nul 2>nul
if errorlevel 1 call :compiler
if errorlevel 1 goto failed
call build.cmd
if errorlevel 1 goto failed
cl /nologo /utf-8 /std:c++17 /O2 /W4 /MT /EHsc tests\PartnerManagerTests.cpp src\UiLanguage.cpp /Fobuild\ /link user32.lib /OUT:build\PartnerManagerTests.exe
if errorlevel 1 goto failed
build\PartnerManagerTests.exe
if errorlevel 1 goto failed
cl /nologo /utf-8 /std:c++17 /O2 /W4 /MT /EHsc tests\BeautyPickerTests.cpp src\UiLanguage.cpp /Fobuild\ /link user32.lib build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /OUT:build\BeautyPickerTests.exe
if errorlevel 1 goto failed
build\BeautyPickerTests.exe
if errorlevel 1 goto failed
if "%~1"=="" goto passed
cl /nologo /utf-8 /std:c++17 /O2 /W4 /MT /EHsc tests\NameTargetProbe.cpp src\ConditionTarget.cpp build\hde64.obj /Fobuild\ /link /BASE:0x180000000 /DYNAMICBASE:NO /OUT:build\NameTargetProbe.exe
if errorlevel 1 goto failed
build\NameTargetProbe.exe "%~1"
if errorlevel 1 goto failed
:passed
popd
exit /b 0
:compiler
set "TASK_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%TASK_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%I in (`"%TASK_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS_INSTALL=%%I"
if not defined TASK_VS_INSTALL exit /b 1
call "%TASK_VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
:usage
echo Usage: tests\run.cmd [path-to-CrimsonDesert.exe]
:failed
popd
exit /b 1
