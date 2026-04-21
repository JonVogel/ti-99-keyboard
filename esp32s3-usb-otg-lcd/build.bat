@echo off
REM Compile and upload Lines demo to ESP32-S3-USB-OTG

setlocal
set PORT=COM5
set FQBN=esp32:esp32:esp32s3
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
arduino-cli compile --fqbn %FQBN% "%SKETCH_DIR%"
goto end

:upload
echo.
echo === Uploading to %PORT% ===
arduino-cli upload -p %PORT% --fqbn %FQBN% "%SKETCH_DIR%"
goto end

:compile_upload
echo.
echo === Compiling ===
arduino-cli compile --fqbn %FQBN% "%SKETCH_DIR%"
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
arduino-cli compile --fqbn %FQBN% "%SKETCH_DIR%"
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
