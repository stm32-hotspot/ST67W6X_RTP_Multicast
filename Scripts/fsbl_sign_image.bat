@echo off

:: Comment the following line to enable the script
exit /B

:: Script logic
set "SEL=FSBL"
set "CWD=%1"
set "IMG_BIN=%CWD%\ST67W6X_RTP_%SEL%.bin"
set "IMG_TRUSTED=%CWD%\ST67W6X_RTP_%SEL%.trusted.bin"
STM32_SigningTool_CLI.exe -s -bin %IMG_BIN% -nk -of 0x80000000 -t fsbl -o %IMG_TRUSTED% -hv 2.3 -dump %IMG_TRUSTED%
