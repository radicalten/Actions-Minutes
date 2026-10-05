#!/bin/sh
# GBA Compilation Builder — one-click start (macOS / Linux)
# Starts the local server and opens your web browser.
cd "$(dirname "$0")"
python3 server.py &
SERVER_PID=$!
sleep 1
if command -v open >/dev/null 2>&1; then open http://localhost:8000
elif command -v xdg-open >/dev/null 2>&1; then xdg-open http://localhost:8000
else echo "Open http://localhost:8000 in your browser"
fi
echo "Server running (PID $SERVER_PID). Press Ctrl+C in this window to stop it."
wait $SERVER_PID
