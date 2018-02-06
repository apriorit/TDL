@echo off

setlocal

set FURUTAKA_CONFIG=Release
set FURUTAKA_PLATFORM=x64
set FURUTAKA_SOLUTION_DIR=%~dp0Source\Furutaka
set FURUTAKA_BINPATH=%FURUTAKA_SOLUTION_DIR%\output\%FURUTAKA_PLATFORM%\%FURUTAKA_CONFIG%\bin
set FURUTAKA_SOLUTION=%FURUTAKA_SOLUTION_DIR%\Furutaka.sln

set DEVENV="%VS140COMNTOOLS%..\IDE\devenv.com"

set CONFIG=Release
set PLATFORM=x64
set SOLUTION_DIR=%~dp0Source\DummyDrv
set BINPATH=%SOLUTION_DIR%\dummy\output\%PLATFORM%\%CONFIG%\bin
set SOLUTION=%SOLUTION_DIR%\dummy.sln

echo.
echo Start build
echo.

set OUTPUTDIR=%~dp0bin
rmdir /S /Q "%OUTPUTDIR%" 2> nul
mkdir "%OUTPUTDIR%" || @echo Can't create folder "%OUTPUTDIR%" && goto :eof

echo.
echo Build driver without options %CONFIG% %PLATFORM%
echo.

%DEVENV% %SOLUTION% /Clean "%CONFIG%|%PLATFORM%" || goto :eof

%DEVENV% %SOLUTION% /Build "%CONFIG%|%PLATFORM%" /project dummy || @echo Error on build %CONFIG% %PLATFORM% dummy && goto :eof

echo.
echo Copy driver without options to output
echo.

echo BINPATH %BINPATH%
echo "F" | xcopy /y "%BINPATH%\dummy.sys" "%OUTPUTDIR%\dummy.sys" || @echo Can`t copy dummy.sys

echo.
echo Build driver with options %CONFIG% %PLATFORM%
echo.

for %%i in (createthread openregistry openfile loadimage honestdriver) do call build_driver_options.bat %%i || @echo Can't build driver with option %%i

echo.
echo Build furutaka %FURUTAKA_CONFIG% %FURUTAKA_PLATFORM%
echo.

%DEVENV% %FURUTAKA_SOLUTION% /Clean "%FURUTAKA_CONFIG%|%FURUTAKA_PLATFORM%" || goto :eof
%DEVENV% %FURUTAKA_SOLUTION% /Build "%FURUTAKA_CONFIG%|%FURUTAKA_PLATFORM%" /project Furutaka || @echo Error on build %FURUTAKA_CONFIG% %FURUTAKA_PLATFORM% Furutaka && goto :eof

echo.
echo Copying Furutaka file to output
echo.

echo "F" | xcopy /y "%FURUTAKA_BINPATH%\Furutaka.exe" "%OUTPUTDIR%\Furutaka.exe" || @echo Can`t copy Furutaka.exe

echo.
echo Delete resource files
echo.

for %%i in (%OUTPUTDIR%\*) do if not %%i == %OUTPUTDIR%\Furutaka.exe del %%i

echo.
echo TDL build completed
echo.

endlocal
