@echo off
setlocal
cd /d "%~dp0"

:: Ensure MinGW bin is in PATH
set "PATH=C:\MinGW\bin;%PATH%"

:: Try MinGW gcc first, then fall back to MSVC
where gcc >nul 2>&1
if %errorlevel% == 0 (
    echo Building with gcc...
    gcc -O2 -o cmatrix.exe cmatrix.c
    goto :done
)

:: Use MSVC via vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: No compiler found. Install gcc or Visual Studio.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (
    `"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set "VS_PATH=%%i"

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
echo Building with MSVC...
cl /O2 /W3 /Fe:cmatrix.exe cmatrix.c >nul
del cmatrix.obj 2>nul

:done
if exist cmatrix.exe (
    echo Done! Run: cmatrix.exe [-s 1-9] [-b]
) else (
    echo Build FAILED.
    exit /b 1
)
endlocal
