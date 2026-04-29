# __ST67W6X RTP Demo__
This application aims to demonstrate the RTP operation with ST67W61

It exercises the following features:

- Camera capture (CSI/DCMIPP)
- H.264 encoding (VENC)
- ST67W6X network driver
- RTP sender

It relies on the FreeRTOS operating system

## __Keywords__
Connectivity, WiFI, ST67W6X_Network_Driver, FreeRTOS, Station mode, UDP, RTP, WPA2

## __Toolchain__
This project is using:

| Item            | Version |
| --------------- | ------- |
| STM32N6_FW      | 1.2.0   |
| X-CUBE-FREERTOS | 1.3.1   |
| X-CUBE-ST67W61  | 1.3.0   |

## __Links and references__
For further information, please visit the ST67W6X [wiki page](https://wiki.st.com/stm32mcu/wiki/Connectivity:Introduction_to_Wi-Fi)

## __Hardware and software environment__
This example runs on the [STM32N6570-DK](https://www.st.com/en/evaluation-tools/stm32n6570-dk.html) board combined with the
[X-NUCLEO-67W61M1](https://www.st.com/en/evaluation-tools/x-nucleo-67w61m1.html) board via the Arduino connectors:

- 5V, 3V3, and GND through the CN6
- SPI (CLK, MOSI, MISO), SPI_CS, and USER_BUTTON through the CN5
- BOOT, CHIP_EN, SPI_RDY, and UART (RX/TX) through the CN9

## __Known limitations__
- Camera/VENC limited to 1280x720@30FPS, 2Mbps
- For Multicast streaming, IGMP should be enabled in the router's options
- The project can be compiled with STM32CubeIDE version 1.19 or lower.

### __Build and flash__

- Build the `FSBL` firmware project using STM32CubeIDE. The resulting binary will be generated at:
    ```
    Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.bin
    ```
- Resort to the [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) to add a header to the binary
  with the following command:
    ```shell
    STM32_SigningTool_CLI.exe -bin Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.bin -nk -of 0x80000000 -t fsbl -o Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.trusted.bin -hv 2.3 -dump Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.trusted.bin
    ```
  The resulting binary is:
    ```
    Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.trusted.bin
    ```
- Connect the device to the HOST (both USB and STLink)

- Open the STM32CubeProgrammer and flash the binary to the board:
    - Set the boot mode in development mode (BOOT1 switch in position 2-3, BOOT0 switch position doesn't matter)
    - Press the reset button
    - Select the external loader `MX66UW1G45G_STM32N6570-DK`
    - Connect to the device
    - Load the trusted binary and flash it at address `0x70000000`
    - Disconnect from the device

- Prepare the serial link to view the application logs:
    - Open a terminal client and connect to the STLink VCOM port
    - Use following configuration: `921600, 8bit data, no parity, 1 stop bit, no flow control` 

- Run the application:
    - Set the boot mode in operation mode (BOOT0 and BOOT1 switch in position 1-2)
    - Press the reset button. The code will run automatically from external flash

### __Connection to an Access Point__

- From the application logs, look for something similar to:
    ```
    ...
    --------------- ST67W6X info ------------
    ST67W6X MW Version:       1.3.0
    AT Version:               1.0.0
    SDK Version:              2.0.106
    Wi-Fi MAC Version:        1.6.44
    BT Controller Version:    1.6.128
    BT Stack Version:         1.10.83
    Build Date:               Mar 20 2026 09:37:01
    Module ID:                C6AFDBD111400004 (-B)
  BOM ID:                   1
    Manufacturing Year:       2024
    Manufacturing Week:       47
    Battery Voltage:          3.316 V
    Trim Wi-Fi hp:            3,3,3,3,3,3,3,3,3,3,4,4,4,4
    Trim Wi-Fi lp:            3,4,4,5,5,6,6,6,6,6,7,7,7,7
    Trim BLE:                 2,2,2,3,4
    Trim XTAL:                41
    MAC Address:              40:82:7b:00:12:28
    Anti-rollback Bootloader: 0
    Anti-rollback App:        0
    -----------------------------------------
    Wifi init ready!
    Ble init is done
    BD Address: 40:82:7b:00:12:29
    Configure BLE
    BLE configuration is done
    
    BLE Commissioning Service Creation
    - BLE service created
    - BLE WIFI Control charac created
    - BLE WIFI Configure charac created
    - BLE WIFI Monitoring charac created
    - BLE services and charac registered
    BLE service and charac creation is done
    
    Start BLE advertising
    BLE advertising is started
  ...
  ```
   The ST67 is correctly advertising and it is ready for the Wi-Fi commissioning over BLE. There are two options to commission the WiFi over BLE, one is using the WebApp and the second option is using the iOS/Android App "ST BLE Toolbox".
  
   
  
    ## __Option 1: WiFi commissioning using the WebApp__
  
    Go to the https://applible.github.io/Web_Bluetooth_App_ST67/
  
  
  
    <img src="Images\comm1web.png" alt="comm1web" style="zoom:50%;" /><img src="Images\comm2web.png" alt="comm2web" style="zoom:50%;" /><img src="Images\comm3web.png" alt="comm3web" style="zoom:50%;" /><img src="Images\comm4web.png" alt="comm3web" style="zoom:50%;" /><img src="Images\comm5web.png" alt="comm3web" style="zoom:50%;" /><img src="Images\comm6web.png" alt="comm3web" style="zoom:50%;" />
  
  
  
   
  
    ## __Option 2: WiFi commissioning using the ST BLE Toolbox__
  
    <img src="Images\comm1app.png" alt="comm1app" style="zoom:90%;" /><img src="Images\comm2app.png" alt="comm2app" style="zoom:90%;" /><img src="Images\comm3app.png" alt="comm3app" style="zoom:90%;" /><img src="Images\comm4app.png" alt="comm4app" style="zoom:90%;" /><img src="Images\comm5app.png" alt="comm5app" style="zoom:90%;" />



- When the ST67 is successfully connected to the Access Point, the following application logs will be shown:

  ```
  ...
   -> BLE CONNECTED [Conn_Handle: 0]
   -> BLE NOTIFICATION ENABLED [Service: 0, Charac: 2].
   -> BLE NOTIFICATION ENABLED [Service: 0, Charac: 3].
   -> BLE WRITE [Conn_Handle: 0, Service: 0, Charac: 0, length 1]
  0x01
  WIFI Control Charac
  WIFI Scan Enable
  WiFi Scan done.
  MAC : [8e:23:00:5c:eb:98] | Channel:  6 |          WPA2 |   AX | RSSI:  -61 | SSID:  FirstSSID
  MAC : [54:94:ee:59:7e:56] | Channel:  6 |          WPA2 |   AX | RSSI:  -65 | SSID:  SecondSSID
  MAC : [65:23:11:fb:97:99] | Channel:  1 |      WPA-WPA2 |    N | RSSI:  -68 | SSID:  ThirdSSID
  BLE Send Notification
  BLE Send Notification
  BLE Send Notification
  BLE Send Notification
   -> BLE WRITE [Conn_Handle: 0, Service: 0, Charac: 1, length 32]
  ...
  WIFI Configure Charac
  SSID NAME
   -> BLE WRITE [Conn_Handle: 0, Service: 0, Charac: 1, length 19]
  ...
  WIFI Configure Charac
  PWD
   -> BLE WRITE [Conn_Handle: 0, Service: 0, Charac: 0, length 1]
  0x03
  WIFI Control Charac
  WIFI Connect
  NCP is treating the connection request
  
  Netif : Link is up
  App connected
  Connected to following Access Point :
  [8e:23:00:5c:eb:98] Channel: 6 | RSSI: -65 | SSID: FirstSSID
  BLE Send Notification
  
  Checking WiFi connection
  
  Application is now running...
  STA IP :
  IP :              10.0.0.37
  Gateway :         10.0.0.1
  Netmask :         255.255.255.0
   -> BLE NOTIFICATION DISABLED [Service: 0, Charac: 2].
   -> BLE NOTIFICATION DISABLED [Service: 0, Charac: 3].
   -> BLE DISCONNECTED [Conn_Handle: 0]
  ...
  ```

  The ST67 is now streaming on the Multicast IP address 239.0.0.0

## Start the RTP stream

Start RTP playback on `ffplay` using the included SDP file.

```shell
ffplay -fflags nobuffer -flags low_delay -protocol_whitelist file,udp,rtp -i rtp.n6dk.sdp
```

The Streaming should start as shown below:

![macOsStreaming](C:\Siana\ST67-73_T02_testing\Firmware\STM32N6570-DK\Publishing\Images\macOsStreaming.png)

![LinuxStreaming](C:\Siana\ST67-73_T02_testing\Firmware\STM32N6570-DK\Publishing\Images\LinuxStreaming.png)

## __Troubleshooting__

### __ST67W6X module FW is incorrect__

- Build the `NCP` firmware project using STM32CubeIDE. The resulting binary will be generated at:
    ```
    Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\NCP\Debug\ST67W6X_RTP_NCP.bin
    ```
- Resort to the [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) to add a header to the binary
  with the following command:
  
    ```shell
    STM32_SigningTool_CLI.exe -bin Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\NCP\Debug\ST67W6X_RTP_NCP.bin -nk -of 0x80000000 -t fsbl -o Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\NCP\Debug\ST67W6X_RTP_NCP.trusted.bin -hv 2.3 -dump Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\NCP\Debug\ST67W6X_RTP_NCP.trusted.bin
    ```
  The resulting binary is:
    ```
    Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\NCP\Debug\ST67W6X_RTP_NCP.trusted.bin
    ```
  
- Connect the device to the HOST (both USB and STLink)

- Open the STM32CubeProgrammer and flash the binary to the board:
    - Set the boot mode in development mode (BOOT1 switch in position 2-3, BOOT0 switch position doesn't matter)
    - Press the reset button
    - Select the external loader `MX66UW1G45G_STM32N6570-DK`
    - Connect to the device
    - Load the trusted binary and flash it at address `0x70000000`
    - Disconnect from the device

- Run the application:
    - Set the boot mode in operation mode (BOOT0 and BOOT1 switch in position 1-2)
    - Press the reset button. The code will run automatically from external flash

- Execute the module update script:
    ```
    X-CUBE-ST67W61\1.3.0\Projects\ST67W6X_Scripts\Binaries\NCP_update_mission_profile_t02.bat
    ```
  And follow the prompts.
