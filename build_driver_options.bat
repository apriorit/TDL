@echo off

setlocal

rem 
rem OPTIONS [createthread|openregistry|openfile|loadimage|honestdriver]
rem

set CMD=%1

echo.
echo Start [%CMD%] driver build
echo.

%DEVENV% %SOLUTION% /Clean "%CONFIG%|%PLATFORM%" || goto :eof

if [%CMD%] == [createthread] (
    set CL=/DMODE_CREATETHREAD
) else if [%CMD%] == [openregistry] (
    set CL=/DMODE_OPENREGISTRY
) else if [%CMD%] == [openfile] (
    set CL=/DMODE_OPENFILE	
) else if [%CMD%] == [loadimage] ( 
    set CL=/DMODE_LOADIMAGE  
) else if [%CMD%] == [honestdriver] ( 
    set CL=/DMODE_HONESTDRIVER  	
) 
echo CL %CL%

echo.
echo Build %SOLUTION% in %CONFIG% %PLATFORM% %CMD%
echo.

%DEVENV% %SOLUTION% /Build "%CONFIG%|%PLATFORM%" /project dummy || @echo Error on build %CONFIG% x86 HarpoonDriver && goto :eof

echo.
echo Copy file to output
echo.

echo "F" | xcopy /y "%BINPATH%\dummy.sys" "%OUTPUTDIR%\dummy_%CMD%.sys" || @echo Can`t copy dummy_%CMD%.sys
endlocal
