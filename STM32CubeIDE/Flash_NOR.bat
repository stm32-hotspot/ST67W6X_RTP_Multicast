SET PATH=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin
SET EXTLOAD="C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"


set "current_dir=%~dp0"
STM32_Programmer_CLI.exe -c port=swd mode=ur -w %current_dir%/FSBL/Debug/ST67W6X_RTP_FSBL-Trusted.bin 0x70000000 -el %EXTLOAD%
