@echo off
rem GBA Compilation Builder - one-click start (Windows)
rem Starts the local server and opens your web browser.
cd /d "%~dp0"
start "" http://localhost:8000
python server.py
