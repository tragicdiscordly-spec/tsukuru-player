@echo off
rem Drag one or more game folders (RPG Maker 2000/2003, XP/VX/Ace, MV, MZ) onto this file to copy them to the PS5's
rem own storage (/data/games). The console must be jailbroken and ftpsrv must be running on it.
rem The console's address is shown at the bottom right of the launcher ("Console: 192.168.1.x").
if "%~1"=="" (
    echo Drag a game folder onto this file to copy it to the PS5.
    echo.
    pause
    exit /b 1
)
if "%PS5_HOST%"=="" set /p PS5_HOST=PS5 address, for example 192.168.1.222:
python "%~dp0scripts\add_game.py" %*
echo.
pause
