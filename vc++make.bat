@echo off
Rem *** This batch file can be used to compile with the **free** version of VC++ toolkit
Rem *** See: http://msdn.microsoft.com/visualc/vctoolkit2003/

if "%1"=="" goto :default
goto :%1

:default
cl /o DVRFlash.exe /nologo *.c*
goto :exit


:clean

del DVRFlash.exe

del dos32aspi.obj
del DVRFlash.obj
del getopt.obj
del scsi.obj
del sgio.obj
del sptx.obj
del stuc.obj
del winaspi.obj


:exit