@echo off
set PATH=d:\djgpp\bin;%PATH%
set DJGPP=d:\djgpp\djgpp.env
gpp -o DVRFlash.exe -Wno-deprecated DVRFlash.cpp scsi.cpp dos32aspi.cpp
pause
