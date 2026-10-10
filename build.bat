@echo off
chcp 65001 >nul
title Mil Recorder Builder
echo ========================================
echo   easy-phi Mil Recorder Builder
echo ========================================
echo.

set GCC=E:\Qt\Tools\mingw1310_64\bin\g++.exe
set SOURCE=E:\grain_data\build\source.cpp
set PATCHED=%SOURCE%.patched.cpp
set OUTPUT=%~dp0mil-recorder.exe


set GRAIN_EXE=
if exist "C:\Users\Lenovo\.workbuddy\binaries\python\envs\default\Scripts\grain.exe" (
    set "GRAIN_EXE=C:\Users\Lenovo\.workbuddy\binaries\python\envs\default\Scripts\grain.exe"
) else (
    for /f "delims=" %%P in ('where python 2^>nul') do (
        if not defined GRAIN_EXE (
            if exist "%%~dpScripts\grain.exe" set "GRAIN_EXE=%%~dpScripts\grain.exe"
        )
    )
)
if not "%GRAIN_EXE%"=="" (
    set "PATH=%GRAIN_EXE%\..;%PATH%"
    echo [OK] Grain found: %GRAIN_EXE%
) else (
    echo [WARN] grain.exe not located automatically; will try system PATH
)


if not exist "%GCC%" (
    echo [ERROR] GCC not found at: %GCC%
    pause & exit /b 1
)
echo [OK] Compiler: %GCC%


set "PATH=E:\Qt\Tools\mingw1310_64\bin;%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;C:\Users\Lenovo\.workbuddy\binaries\python\envs\default\Scripts;C:\Users\Lenovo\.workbuddy\binaries\python\envs\default"


set APP_MACRO=APP_TYPE_MIL_PLAYER
if not exist "%SOURCE%" (
    echo [INFO] Running grain to prepare source for %APP_MACRO%...
    where grain 2>nul
    if errorlevel 1 (
        echo [ERROR] grain not installed. Run: pip install E:\grain
        pause & exit /b 1
    )
    cd /d "%~dp0files\test"
    grain package run-test %~dp0 --macro %APP_MACRO%
    if not exist "%SOURCE%" ( echo [ERROR] grain failed to generate source & pause & exit /b 1 )
    cd /d "%~dp0"
)
echo [OK] Source: %SOURCE%


set SRC=%PATCHED%
echo [PATCH] Applying GCC 13 compatibility fixes...
python "%~dp0patch_gcc13.py" "%SOURCE%"
if not exist "%PATCHED%" set SRC=%SOURCE%


echo.
echo [COMPILE] Starting compilation (this may take a few minutes)...
echo.

set INCS=-IE:\grain_data\packages\gglfw3==0\.grain\includes
set INCS=%INCS% -IE:\grain_data\packages\gimage==0\.grain\includes
set INCS=%INCS% -IE:\grain_data\packages\gvorbis==0\.grain\includes
set INCS=%INCS% -IE:\grain_data\packages\gminiaudio==0\.grain\includes
set INCS=%INCS% -IE:\grain_data\packages\gvideoenc==0\.grain\includes
set INCS=%INCS% -IE:\grain_data\packages\gtext_renderer==0\.grain\includes

set LIBS=E:\grain_data\packages\gglfw3==0\.grain\libs\libglfw3_windows_x86_64.a
set LIBS=%LIBS% E:\grain_data\packages\gimage==0\.grain\libs\libstb_image_windows_x86_64.a
set LIBS=%LIBS% E:\grain_data\packages\gvorbis==0\.grain\libs\libstb_vorbis_windows_x86_64.a
set LIBS=%LIBS% E:\grain_data\packages\gminiaudio==0\.grain\libs\libminiaudio_windows_x86_64.a
set LIBS=%LIBS% E:\grain_data\packages\gvideoenc==0\.grain\libs\libavformat.a
set LIBS=%LIBS% E:\grain_data\packages\gvideoenc==0\.grain\libs\libavcodec.a
set LIBS=%LIBS% E:\grain_data\packages\gvideoenc==0\.grain\libs\libavutil.a
set LIBS=%LIBS% E:\grain_data\packages\gvideoenc==0\.grain\libs\libx264.a
set LIBS=%LIBS% E:\grain_data\packages\gvideoenc==0\.grain\libs\libfdk-aac.a
set LIBS=%LIBS% E:\grain_data\packages\gtext_renderer==0\.grain\libs\libstb_truetype_windows_x86_64.a

set LFLAGS=-lbcrypt -luuid -lole32 -lcomctl32 -lopengl32 -lgdi32 -lws2_32

echo [BUILD] Using source: %SRC%
"%GCC%" -std=c++20 -static -O1 -Wsign-compare -Wa,-mbig-obj %INCS% "%SRC%" %LIBS% %LFLAGS% -o "%OUTPUT%"

echo.
if exist "%OUTPUT%" (
    echo [SUCCESS] Done! Output: %OUTPUT%
) else (
    echo [FAILED] Compilation failed
    echo Check the error messages above.
)
echo.
pause
