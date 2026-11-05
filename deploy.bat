@echo off
rem Desktop shortcut target ("SpaceGod - Unreal C++"). Asks first: 1) Deploy & Run pulls latest, packages even when
rem nothing changed (-Force), publishes to C:\Apps\SpaceGod.Unreal.Cpp, refreshes the shortcut and launches it
rem (run.ps1 does the work); 2) Run starts the published copy without deploying (deploying first if there is none).
rem "deploy.bat --deploy [flags]" skips the menu and deploys without -Force (the /deploy hook); any other arguments
rem pass through to run.ps1 when deploying.
setlocal
title SpaceGod - Unreal C++
set "APP=C:\Apps\SpaceGod.Unreal.Cpp"
set "EXE=%APP%\SpaceGod.Unreal.Cpp.exe"
set "FORCE=-Force"
set "ARGS=%*"
if /i not "%~1"=="--deploy" goto menu
set "FORCE="
set "ARGS="
shift
:collect
if "%~1"=="" goto deploy
set "ARGS=%ARGS% %1"
shift
goto collect

:menu
echo.
echo   SpaceGod - Unreal C++
echo     1^) Deploy ^& Run
echo     2^) Run
echo.
choice /c 12 /n /m "  Choose 1 or 2: "
if errorlevel 2 goto run

:deploy
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" -Launch %FORCE% %ARGS%
if errorlevel 1 (
    echo.
    echo   Deploy failed. See the output above.
    pause
    exit /b 1
)
timeout /t 5
exit /b 0

:run
if not exist "%EXE%" (
    echo.
    echo   SpaceGod - Unreal C++ is not deployed yet ^(%EXE% is missing^) - deploying first.
    set "FORCE="
    goto deploy
)
start "" /d "%APP%" "%EXE%" -capture video hq
exit /b 0
