@echo off
rem Double-click after the PS5 has been jailbroken (repeat after every reboot of the console).
rem Sends the web launcher and FTP server to the PS5 and opens the launcher page.
python "%~dp0scripts\start_ps5.py" %*
echo.
pause
