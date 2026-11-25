@echo off
setlocal
set EXE=%~dp0build\src\Debug\c3_extract.exe
powershell -Command "& '%EXE%' %*"
