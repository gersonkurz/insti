@echo off
setlocal

set BUILD_DIR=build-x64
set CONFIG=Release

if "%1"=="clear" goto clear
if "%1"=="debug" goto debug
if "%1"=="release" goto release
if "%1"=="test" goto test
if "%1"=="" goto release
echo Unknown command: %1
echo Usage: build [clear^|debug^|release^|test]
exit /b 1

:clear
echo Removing %BUILD_DIR%...
if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%
echo Done.
exit /b 0

:debug
set CONFIG=Debug
goto build

:release
set CONFIG=Release
goto build

:build
if not exist %BUILD_DIR% (
    echo Configuring CMake...
    cmake -B %BUILD_DIR% -A x64
    if errorlevel 1 exit /b 1
)
echo Building %CONFIG%...
cmake --build %BUILD_DIR% --config %CONFIG%
exit /b %errorlevel%

:test
if not exist %BUILD_DIR% (
    echo Build directory not found. Run "build release" first.
    exit /b 1
)
echo Running tests...
%BUILD_DIR%\tests\Release\engramma_tests.exe
exit /b %errorlevel%
