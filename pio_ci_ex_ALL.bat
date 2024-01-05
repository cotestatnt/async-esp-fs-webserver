@echo off
rem Build all listed examples of this library on PIO CLI; may take some time
rem Verifies that library and all examples still compile

setlocal EnableDelayedExpansion enableextensions
set CI_DIR=%~dp0\..\async-esp-fs-webserver.pio-ci
set EXAMPLES=simpleServerCaptive simpleServer withWebSocket customHTML customOptions gpio_list handleFormData highcharts remoteOTA esp32-cam
::set EXAMPLES=simpleServerCaptive
:: simpleServer withWebSocket customHTML
FOR  %%E IN (%EXAMPLES%) DO IF NOT EXIST %CI_DIR%\ci_ex_%%E MKDIR %CI_DIR%\ci_ex_%%E
@echo on
FOR  %%E IN (%EXAMPLES%) DO (
 pio ci  -c platformio.ini --board=esp32dev --keep-build-dir --build-dir=%CI_DIR%\ci_ex_%%E --lib=. .\examples\%%E\*.* > %CI_DIR%\ci_ex_%%E\build.out.txt 2>%CI_DIR%\ci_ex_%%E\build.err.txt
 @for /f %%i in ("%CI_DIR%\ci_ex_%%E\build.err.txt") do @set size=%%~zi && @echo ###ERROR in %%i && @type %%i
 )
@echo off
:: type %CI_DIR%/ci_ex_%%E/build.err.txt
:: note activation pio verbose option '-v' outputs a lot of text (~25k/compile, ~2MB/pio-ci)
rem pio ci  -c platformio.ini --board=esp32dev --build-dir=../ci_ex_simpleServer --keep-build-dir --lib=. .\examples\simpleServer\simpleServer.ino
