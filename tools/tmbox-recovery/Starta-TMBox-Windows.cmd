@echo off
setlocal
where py >nul 2>nul
if errorlevel 1 goto try_python
py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>nul
if errorlevel 1 goto try_python
py -3 "%~dp0meny.py" %*
set "TMBOX_EXIT=%ERRORLEVEL%"
goto finish
:try_python
where python >nul 2>nul
if errorlevel 1 goto missing_python
python -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>nul
if errorlevel 1 goto missing_python
python "%~dp0meny.py" %*
set "TMBOX_EXIT=%ERRORLEVEL%"
goto finish
:missing_python
echo Python 3.10 eller senare behovs. Installera fran python.org och las LAS-MIG.txt.
set "TMBOX_EXIT=1"
:finish
if not "%TMBOX_MENU_NO_PAUSE%"=="1" pause
exit /b %TMBOX_EXIT%
