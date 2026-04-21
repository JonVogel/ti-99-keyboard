@echo off
REM Compile and upload TI BASIC sketch to ESP32-8048S043C (Sunton 4.3" RGB)
REM Usage:
REM   build.bat           - compile + upload
REM   build.bat compile   - compile only
REM   build.bat upload    - upload only (uses last compiled binary)
REM   build.bat monitor   - open serial monitor
REM   build.bat all       - compile + upload + monitor

setlocal
set PORT=COM10
set FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default
set SKETCH_DIR=%~dp0

REM Remove trailing backslash
if %SKETCH_DIR:~-1%==\ set SKETCH_DIR=%SKETCH_DIR:~0,-1%

if "%1"=="" goto compile_upload
if /i "%1"=="compile" goto compile
if /i "%1"=="upload" goto upload
if /i "%1"=="monitor" goto monitor
if /i "%1"=="all" goto all
goto compile_upload

:compile
echo.
echo === Compiling ===
arduino-cli compile --fqbn %FQBN% --libraries "%SKETCH_DIR%\.." "%SKETCH_DIR%"
goto end

:upload
echo.
echo === Uploading to %PORT% ===
arduino-cli upload -p %PORT% --fqbn %FQBN% "%SKETCH_DIR%"
goto end

:compile_upload
echo.
echo === Compiling ===
arduino-cli compile --fqbn %FQBN% --libraries "%SKETCH_DIR%\.." "%SKETCH_DIR%"
if errorlevel 1 goto end
echo.
echo === Uploading to %PORT% ===
arduino-cli upload -p %PORT% --fqbn %FQBN% "%SKETCH_DIR%"
goto end

:monitor
echo.
echo === Monitoring %PORT% (Ctrl+C to exit) ===
arduino-cli monitor -p %PORT% --config baudrate=115200
goto end

:all
echo.
echo === Compiling ===
arduino-cli compile --fqbn %FQBN% --libraries "%SKETCH_DIR%\.." "%SKETCH_DIR%"
if errorlevel 1 goto end
echo.
echo === Uploading to %PORT% ===
arduino-cli upload -p %PORT% --fqbn %FQBN% "%SKETCH_DIR%"
if errorlevel 1 goto end
echo.
echo === Monitoring %PORT% (Ctrl+C to exit) ===
arduino-cli monitor -p %PORT% --config baudrate=115200
goto end

:end
endlocal
