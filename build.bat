@echo off

SET Z88DK_DIR=c:\z88dk\
SET ZCCCFG=%Z88DK_DIR%lib\config\
SET PATH=%Z88DK_DIR%bin;%PATH%

echo.
echo ****************************************************************************
echo  CPPIP / NPPIP / FPPIP Build Script
echo ****************************************************************************

if /I "%1"=="debug" goto build_debug

:build_release
echo  Building CPPIP.COM (standard — IA available via /N, SD available)...
zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DNABU_IA -DFREHD ^
    -I..\NABULIB ^
    ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c sdio.c console.c ^
    -o CPPIP
if errorlevel 1 goto fail

echo  Building NPPIP.COM (NABU edition — IA always active)...
zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DNABU_IA -DNABU_DEFAULT ^
    -I..\NABULIB ^
    ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c ^
    -o NPPIP
if errorlevel 1 goto fail

echo  Building FPPIP.COM (FreHD edition — SD always active)...
zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DFREHD -DFREHD_DEFAULT ^
    ppip.c cmdparse.c filename.c diskio.c crc.c sdio.c console.c ^
    -o FPPIP
if errorlevel 1 goto fail

goto sizes

:build_debug
echo  Building CPPIP.COM (debug)...
zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DNABU_IA -DDEBUG ^
    -I..\NABULIB ^
    ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c ^
    -o CPPIP
if errorlevel 1 goto fail
goto sizes

:sizes
echo.
echo  Output:
for %%F in (CPPIP.COM NPPIP.COM FPPIP.COM) do (
    if exist %%F echo    %%F  %%~zF bytes
)
echo.
echo ****************************************************************************
echo  Build OK. Usage: build.bat [debug]
echo ****************************************************************************
exit /b 0

:fail
echo.
echo  *** BUILD FAILED ***
echo ****************************************************************************
exit /b 1
