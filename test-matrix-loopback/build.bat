@echo off
REM Compile and upload the MATRIX LOOPBACK TEST (not the keyboard
REM firmware -- that's the build.bat in the repo root). Run this one
REM from inside test-matrix-loopback\.
REM
REM Usage:
REM   build.bat                   - compile + upload (default port)
REM   build.bat compile           - compile only
REM   build.bat upload            - upload only
REM   build.bat monitor           - serial monitor
REM   build.bat all               - compile + upload + monitor
REM
REM Each form accepts an optional COM port (Windows reassigns ports
REM on every replug):
REM   build.bat upload  COM23
REM   build.bat all     COM7
REM   build.bat         COM5     (compile+upload on COM5)
REM   build.bat monitor COM4

setlocal enabledelayedexpansion

set "DEFAULT_PORT=COM5"
set "ACTION=%~1"
set "PORT_ARG=%~2"

REM Bare COM-port arg with no action.
REM Must start with "COM" AND have a digit at position 3 — otherwise
REM "compile" (also starts with "com" case-insensitive) gets misread.
if "!PORT_ARG!"=="" (
  set "FIRST=!ACTION!"
  if /i "!FIRST:~0,3!"=="COM" (
    set "C4=!FIRST:~3,1!"
    set "IS_PORT=0"
    for %%D in (0 1 2 3 4 5 6 7 8 9) do (
      if "!C4!"=="%%D" set "IS_PORT=1"
    )
    if "!IS_PORT!"=="1" (
      set "PORT_ARG=!ACTION!"
      set "ACTION="
    )
  )
)

if "!PORT_ARG!"=="" (
  set "PORT=!DEFAULT_PORT!"
) else (
  set "PORT=!PORT_ARG!"
)

set "FQBN=esp32:esp32:esp32s3:PartitionScheme=no_ota,FlashSize=16M,PSRAM=opi"
set "SKETCH_DIR=%~dp0"
if "!SKETCH_DIR:~-1!"=="\" set "SKETCH_DIR=!SKETCH_DIR:~0,-1!"

echo.
echo === build.bat (MATRIX LOOPBACK TEST): action='!ACTION!' port='!PORT!' ===

if "!ACTION!"==""           goto compile_upload
if /i "!ACTION!"=="compile" goto compile
if /i "!ACTION!"=="upload"  goto upload
if /i "!ACTION!"=="monitor" goto monitor
if /i "!ACTION!"=="all"     goto all
goto compile_upload

:compile
echo.
echo === Compiling ===
arduino-cli compile --fqbn !FQBN! --libraries "!SKETCH_DIR!" "!SKETCH_DIR!"
goto end

:upload
echo.
echo === Uploading to !PORT! ===
arduino-cli upload -p !PORT! --fqbn !FQBN! "!SKETCH_DIR!"
goto end

:compile_upload
echo.
echo === Compiling ===
arduino-cli compile --fqbn !FQBN! --libraries "!SKETCH_DIR!" "!SKETCH_DIR!"
if errorlevel 1 goto end
echo.
echo === Uploading to !PORT! ===
arduino-cli upload -p !PORT! --fqbn !FQBN! "!SKETCH_DIR!"
goto end

:monitor
echo.
echo === Monitoring !PORT! (Ctrl+C to exit) ===
arduino-cli monitor -p !PORT! --config baudrate=115200
goto end

:all
echo.
echo === Compiling ===
arduino-cli compile --fqbn !FQBN! --libraries "!SKETCH_DIR!" "!SKETCH_DIR!"
if errorlevel 1 goto end
echo.
echo === Uploading to !PORT! ===
arduino-cli upload -p !PORT! --fqbn !FQBN! "!SKETCH_DIR!"
if errorlevel 1 goto end
echo.
echo === Monitoring !PORT! (Ctrl+C to exit) ===
arduino-cli monitor -p !PORT! --config baudrate=115200
goto end

:end
endlocal
