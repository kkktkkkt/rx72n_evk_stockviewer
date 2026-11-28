@echo off
rem start job-runner (double-click). close this window to stop.
rem status: unverified
cd /d "%~dp0.."
:loop
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0job-runner.ps1"
if errorlevel 75 goto loop
pause
