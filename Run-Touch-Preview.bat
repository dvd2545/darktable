@echo off
setlocal
set "PREVIEW_DIR=%~dp0build"
if exist "%~dp0bin\darktable.exe" set "PREVIEW_DIR=%~dp0"
set "RUNTIME_DIR=%LOCALAPPDATA%\CodexTools\msys64\ucrt64\bin"

if not exist "%PREVIEW_DIR%\bin\darktable.exe" (
  echo The darktable test build was not found in "%PREVIEW_DIR%\bin".
  pause
  exit /b 1
)
if not exist "%RUNTIME_DIR%\libgtk-3-0.dll" (
  echo The test build runtime was not found in "%RUNTIME_DIR%".
  pause
  exit /b 1
)

set "PATH=%RUNTIME_DIR%;%PREVIEW_DIR%\bin;%PATH%"
start "" /D "%PREVIEW_DIR%" "%PREVIEW_DIR%\bin\darktable.exe" ^
  --datadir "%PREVIEW_DIR%\share\darktable" ^
  --moduledir "%PREVIEW_DIR%\lib\darktable" ^
  --configdir "%PREVIEW_DIR%\touch-profile" ^
  --cachedir "%PREVIEW_DIR%\touch-cache" ^
  --library "%PREVIEW_DIR%\touch-profile\touch-test-library.db" ^
  --conf ui_last/theme=darktable-touch ^
  --conf themes/condensed=false ^
  --conf themes/usercss=false ^
  --conf write_sidecar_files=never
exit /b %ERRORLEVEL%
