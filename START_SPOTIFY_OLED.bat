@echo off
title Spotify OLED Stream
echo ========================================
echo   Spotify to ESP32 OLED Streamer
echo ========================================
echo.
cd /d "%~dp0"
".venv\Scripts\python.exe" spotify_sender.py
pause
