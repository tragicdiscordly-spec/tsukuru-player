@echo off
rem Drag a game folder (RPG Maker MV or MZ) onto this file to make PS5-playable copies of its movies (.mpg next to the
rem .webm / .mp4). Needs ffmpeg on your PC: winget install Gyan.FFmpeg
if "%~1"=="" (
    echo Drag a game folder onto this file to convert its movies for the PS5.
    echo.
    pause
    exit /b 1
)
python "%~dp0scripts\convert_video.py" %*
echo.
pause
