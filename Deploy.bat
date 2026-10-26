@echo off
setlocal EnableExtensions

REM Copies the ASWToml library sources, build files, docs and example to a folder, e.g. for a release package or to
REM drop into another project. Usage: Deploy.bat "C:\path\to\staging"

set "SCRIPT_DIR=%~dp0"
set "TARGET_DIR=%~1"

if "%TARGET_DIR%"=="" (
    echo ERROR: A staging/deploy folder is required.
    echo Usage: Deploy.bat "C:\path\to\staging"
    exit /b 1
)

echo Deploying ASWToml release files to:
echo   %TARGET_DIR%

if not exist "%TARGET_DIR%\." (
    echo Creating target folder...
    mkdir "%TARGET_DIR%"
    if errorlevel 1 (
        echo ERROR: Could not create target folder.
        exit /b 1
    )
)

if not exist "%TARGET_DIR%\ASWToml\." (
    echo Creating ASWToml folder...
    mkdir "%TARGET_DIR%\ASWToml"
    if errorlevel 1 (
        echo ERROR: Could not create "%TARGET_DIR%\ASWToml".
        exit /b 1
    )
)

call :CopyFile "CHANGELOG.md" "%TARGET_DIR%\CHANGELOG.md"
if errorlevel 1 exit /b 1
call :CopyFile "CMakeLists.txt" "%TARGET_DIR%\CMakeLists.txt"
if errorlevel 1 exit /b 1
call :CopyFile "LICENSE" "%TARGET_DIR%\LICENSE"
if errorlevel 1 exit /b 1
call :CopyFile "README.md" "%TARGET_DIR%\README.md"
if errorlevel 1 exit /b 1

call :CopyFile "cmake\ASWTomlConfig.cmake.in" "%TARGET_DIR%\cmake\ASWTomlConfig.cmake.in"
if errorlevel 1 exit /b 1

call :CopyFile "ASWToml\ASWToml_Base.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Base.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Base.h" "%TARGET_DIR%\ASWToml\ASWToml_Base.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Converters.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Converters.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Converters.h" "%TARGET_DIR%\ASWToml\ASWToml_Converters.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Document.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Document.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Document.h" "%TARGET_DIR%\ASWToml\ASWToml_Document.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_File.cpp" "%TARGET_DIR%\ASWToml\ASWToml_File.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_File.h" "%TARGET_DIR%\ASWToml\ASWToml_File.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Index.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Index.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Index.h" "%TARGET_DIR%\ASWToml\ASWToml_Index.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Interface.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Interface.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Interface.h" "%TARGET_DIR%\ASWToml\ASWToml_Interface.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Options.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Options.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Options.h" "%TARGET_DIR%\ASWToml\ASWToml_Options.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Parser.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Parser.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Parser.h" "%TARGET_DIR%\ASWToml\ASWToml_Parser.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Path.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Path.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Path.h" "%TARGET_DIR%\ASWToml\ASWToml_Path.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Scalars.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Scalars.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Scalars.h" "%TARGET_DIR%\ASWToml\ASWToml_Scalars.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Settings.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Settings.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Settings.h" "%TARGET_DIR%\ASWToml\ASWToml_Settings.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Types.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Types.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Types.h" "%TARGET_DIR%\ASWToml\ASWToml_Types.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Utils.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Utils.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Utils.h" "%TARGET_DIR%\ASWToml\ASWToml_Utils.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Value.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Value.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Value.h" "%TARGET_DIR%\ASWToml\ASWToml_Value.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Version.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Version.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Version.h" "%TARGET_DIR%\ASWToml\ASWToml_Version.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Writer.cpp" "%TARGET_DIR%\ASWToml\ASWToml_Writer.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWToml\ASWToml_Writer.h" "%TARGET_DIR%\ASWToml\ASWToml_Writer.h"
if errorlevel 1 exit /b 1

call :CopyFile "example\cmake\CMakeLists.txt" "%TARGET_DIR%\example\cmake\CMakeLists.txt"
if errorlevel 1 exit /b 1
call :CopyFile "example\cmake\README.md" "%TARGET_DIR%\example\cmake\README.md"
if errorlevel 1 exit /b 1
call :CopyFile "example\rad370\ASWTomlExample.cbproj" "%TARGET_DIR%\example\rad370\ASWTomlExample.cbproj"
if errorlevel 1 exit /b 1
call :CopyFile "example\rad370\Build_Win64x_Debug.bat" "%TARGET_DIR%\example\rad370\Build_Win64x_Debug.bat"
if errorlevel 1 exit /b 1
call :CopyFile "example\rad370\Build_Win64x_Release.bat" "%TARGET_DIR%\example\rad370\Build_Win64x_Release.bat"
if errorlevel 1 exit /b 1
call :CopyFile "example\src\main.cpp" "%TARGET_DIR%\example\src\main.cpp"
if errorlevel 1 exit /b 1

echo Deployment completed successfully.
exit /b 0

:CopyFile
set "SOURCE_FILE=%~1"
set "DESTINATION_FILE=%~2"
echo Copying %SOURCE_FILE%...
if not exist "%SCRIPT_DIR%%SOURCE_FILE%" (
    echo ERROR: Source file not found: "%SCRIPT_DIR%%SOURCE_FILE%"
    exit /b 1
)
if not exist "%DESTINATION_FILE%\.." (
    mkdir "%DESTINATION_FILE%\.."
)
copy /Y "%SCRIPT_DIR%%SOURCE_FILE%" "%DESTINATION_FILE%" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy "%SOURCE_FILE%".
    exit /b 1
)
exit /b 0
