@echo off
rem Compila y corre el test del motor sin JUCE (segundos, en vez de los minutos del plugin).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
cl /nologo /std:c++20 /O2 /EHsc /utf-8 /W3 /fp:fast /I ..\Source\dsp /Fo:out\ /Fe:out\CincelDspTest.exe DspTest.cpp /link /STACK:16777216
if errorlevel 1 exit /b 1
out\CincelDspTest.exe %*
