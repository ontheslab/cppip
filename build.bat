@echo off

SET Z88DK_DIR=c:\z88dk\
SET ZCCCFG=%Z88DK_DIR%lib\config\
SET PATH=%Z88DK_DIR%bin;%PATH%

echo.
echo ****************************************************************************
echo  CPPIP Build Script
echo ****************************************************************************

if /I "%1"=="nabu"  goto build_nabu
if /I "%1"=="debug" goto build_debug

:build_release
echo  Building CPPIP (plain CP/M release)...
zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size ^
    ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c ^
    -o CPPIP
goto done

:build_nabu
echo  Building CPPIP (NABU IA release)...
zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DNABU_IA ^
    -I..\NABULIB ^
    ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c ^
    -o CPPIP
goto done

:build_debug
echo  Building CPPIP (debug)...
zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DDEBUG ^
    ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c ^
    -o CPPIP

:done
echo ****************************************************************************
echo  Done. Usage: build.bat [debug]
echo ****************************************************************************
pause
