@echo off
set PATH=g:\djgpp\bin;%PATH%
set DJGPP=g:\djgpp\djgpp.env
gpp -o DVRFlash.exe -Wno-deprecated *.c*
pause
