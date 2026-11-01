@echo off
setlocal
pushd "%~dp0"
if not "%~1"=="" if /I not "%~1"=="input-helper" (
  echo Usage: build.cmd [input-helper]
  popd
  exit /b 2
)
where cl.exe >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  call :find_compiler
  if errorlevel 1 (
    popd
    exit /b 1
  )
)
if not exist build mkdir build
if /I "%~1"=="input-helper" goto input_helper
cl /nologo /std:c++17 /O2 /W4 /MT /LD /EHsc src\CohabitationGateExperiment.cpp /Fobuild\CohabitationGateExperiment.obj /link /OUT:build\CohabitationGateExperiment.asi
if errorlevel 1 goto failed
copy /Y config\CohabitationGateExperiment.ini build\CohabitationGateExperiment.ini >nul
echo Built build\CohabitationGateExperiment.asi. No files installed into the game.
popd
exit /b 0

:input_helper
cl /nologo /std:c++17 /O2 /W4 /MT /EHsc tools\input-helper\GameKeyHold.cpp /Fobuild\GameKeyHold.obj /Febuild\GameKeyHold.exe user32.lib
if errorlevel 1 goto failed
echo Built build\GameKeyHold.exe. The helper has NOT been executed.
popd
exit /b 0

:find_compiler
if not exist "%VSWHERE%" (
  echo Install Visual Studio Build Tools with Desktop development with C++, or use an x64 Native Tools prompt.
  exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS_INSTALL=%%I"
if not defined TASK_VS_INSTALL (
  echo No Visual Studio C++ x64 toolchain found.
  exit /b 1
)
call "%TASK_VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%

:failed
popd
exit /b 1
