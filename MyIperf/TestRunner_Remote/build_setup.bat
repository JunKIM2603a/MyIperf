@echo off
echo ================================================
echo TestRunner2 Build Setup Script
echo ================================================

REM Create build directory if it doesn't exist
if not exist build mkdir build

REM Navigate to build directory
cd build

echo.
echo Setting up MSVC environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo Running CMake configuration...
cmake ..

if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo Building TestRunner2 (Release)...
cmake --build . --config Release

if errorlevel 1 (
    echo.
    echo ERROR: Build failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo ================================================
echo Build completed successfully!
echo ================================================
echo.
echo Executable location:
echo   build\Release\TestRunner2.exe
echo.

cd ..
pause

