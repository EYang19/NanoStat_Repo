@echo off
setlocal
cd /d "%~dp0"

echo Starting NanoStat Web Console server at http://localhost:8000
echo Keep the server window open while using the web app.

start "NanoStat Web Server" /D "%~dp0" cmd /k "python -m http.server 8000"

timeout /t 1 /nobreak >nul
start "" msedge "http://localhost:8000"
