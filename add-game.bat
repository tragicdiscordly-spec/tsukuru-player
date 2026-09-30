@echo off
rem Drag one or more RPG Maker 2000/2003 game folders onto this file to copy them to the PS5.
rem The console must be jailbroken and start-ps5.bat must have been run first.
if "%~1"=="" (
    echo Drag a game folder onto this file to copy it to the PS5.
    echo.
    pause
    exit /b 1
)
python "%~dp0scripts\add_game.py" %*
echo.
pause
