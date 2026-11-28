@echo off
rem OPTIONAL: start job-runner at Windows logon (minimized window). status: unverified
rem Remove with: schtasks /delete /tn rx72n-job-runner /f
schtasks /create /tn rx72n-job-runner /sc onlogon /rl limited /f /tr "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Minimized -File D:\workspace\rx72n\rx72n_stockview\tools\job-runner.ps1"
pause
