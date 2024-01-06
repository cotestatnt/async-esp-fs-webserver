@echo off
rem Build all listed examples of this library on PIO CLI; may take some time
rem Verifies that library and all examples still compile

setlocal EnableDelayedExpansion enableextensions
set CI_DIR=%~dp0..\async-esp-fs-webserver.pio-ci
set EXAMPLES=simpleServerCaptive simpleServer withWebSocket customHTML customOptions gpio_list handleFormData highcharts remoteOTA esp32-cam
::set EXAMPLES=simpleServerCaptive
:: simpleServer withWebSocket customHTML
set BOARDS= esp32dev esp12e
set OPT_esp12e=-O "lib_deps=ArduinoJson@6.21.4"
set OPT_esp32dev=-O "lib_deps=ArduinoJson@6.21.4"
set NOT_esp12e=esp32-cam
set NOT_esp32dev=esp8266-app
set EXCLIB= --exclude=lib\*\.git* --exclude=lib\*\.pio --exclude=lib\*\.vscode --exclude=lib\*\built-in-webpages --exclude=lib\*\examples

FOR  %%B IN (%BOARDS%) DO (
  FOR %%E IN (%EXAMPLES%) DO (
    if %%E==!NOT_%%B! (
      echo ### not compiling %%E for %%B
    ) else (
      set CIEXDIR=%CI_DIR%\ci_ex_%%B_%%E
      IF EXIST "!CIEXDIR!" RMDIR /s/q "!CIEXDIR!"
      MKDIR "!CIEXDIR!"
      set OPT=!OPT_%%B!
      set OUT="!CIEXDIR!\build.out.txt"
      set ERR="!CIEXDIR!\build.err.txt"
      echo ### Compiling %%E for %%B
      echo +pio ci  -b %%B !OPT! --keep-build-dir --build-dir="!CIEXDIR!\" --lib=. %EXCLIB%  .\examples\%%E\*.*   1^>!OUT!  2^>!ERR!
      pio ci -b %%B !OPT! --keep-build-dir --build-dir="!CIEXDIR!" --lib=. %EXCLIB%  .\examples\%%E\*.*   >!OUT!  2>!ERR!
      FOR /f %%i IN (!ERR!) DO IF EXIST %%i IF %%~zi GTR 0 ECHO ###ERROR in %%i && TYPE %%i
    )
    echo ### DONE
  )
)

:: note activation pio verbose option '-v' generates a lot of output (~25k/compile, ~2MB/pio-ci)

