@echo off
REM Compile and upload TI-99/4A Keyboard Adapter to ESP32-S3

setlocal
set PORT=COM5
set FQBN=esp32:esp32:esp32s3:PartitionScheme=no_ota,FlashSize=16M,PSRAM=opi
set SKETCH_DIR=%~dp0

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
arduino-cli compile --fqbn %FQBN% --libraries "%SKETCH_DIR%" "%SKETCH_DIR%"
goto end

:upload
echo.
echo === Uploading to %PORT% ===
arduino-cli upload -p %PORT% --fqbn %FQBN% "%SKETCH_DIR%"
goto end

:compile_upload
echo.
echo === Compiling ===
arduino-cli compile --fqbn %FQBN% --libraries "%SKETCH_DIR%" "%SKETCH_DIR%"
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
arduino-cli compile --fqbn %FQBN% --libraries "%SKETCH_DIR%" "%SKETCH_DIR%"
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
