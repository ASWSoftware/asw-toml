@echo off

REM Builds the ASWToml example with RAD Studio (Win64x, Debug). Requires the Rad370 environment variable.

call "%Rad370%\bin\rsvars.bat"

pushd "%~dp0"
call cmd /c msbuild /t:Build /p:Config=Debug;Platform=Win64x ASWTomlExample.cbproj
set "BUILD_RESULT=%ERRORLEVEL%"
popd

exit /b %BUILD_RESULT%
