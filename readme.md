# __ST67W6X RTP Demo__
This application demonstrates a complete connected multimedia use case on the STM32N6570-DK with ST67W6X

It exercises the following features:

- Camera capture (CSI/DCMIPP)
- H.264 encoding (VENC)
- ST67W6X network driver
- Wi-Fi commissioning over BLE
- MQTT (over TLS) telemetry and subscription
- RTP H.264 video streaming
- Splash screen frame shown while stream is paused

## __Keywords__
Connectivity, WiFi, ST67W6X*_Network_*Driver, FreeRTOS, Station mode, BLE, UDP, RTP, H264, MQTT, TLS, WPA2

## Overview

This application combines several functions in a single demo:

1. The board starts in **BLE advertising** mode

2. A mobile app or Web Bluetooth page is used to provision **Wi-Fi credentials**
3. Once Wi-Fi is connected: 
   - MQTT **is started**
   - RTP H.264 streaming **is started**
4. The *USER button* behavior depends on the current state:
   - if BLE commissioning is active or no stream is running, it is used for BLE security / unpair handling  
   - if Wi-Fi is connected and RTP is active, it toggles **pause/resume** of the video stream 
5. When the stream is paused, the application sends a prebuilt **H.264 splash frame** instead of live camera frames.



### **Main implemented features**

#### BLE Wi-Fi commissioning

The BLE commissioning service supports

- Wi-Fi scan trigger
- AP list notifications
- SSID configuration
- Password configuration
- Wi-Fi connect / disconnect requests
- Wi-Fi monitoring notifications
- BLE pairing and passkey handling

#### MQTT over TLS

The application now includes an MQTT client that connects securely to a broker using **TLS**.

Implemented capabilities include:

- DNS resolution of the broker hostname
- TCP socket creation
- TLS session establishment
- MQTT client connection
- periodic telemetry publishing
- topic subscription for receiving control messages
- JSON payload parsing
- basic remote command handling

By default, the MQTT configuration is set for:

- Broker host: `broker.emqx.io`
- Port: `8883`
- TLS enabled
- Server Name Indication (SNI) enabled

Supported MQTT security schemes in the code are:

- `0`: plain TCP
- `1`: TLS with username/password
- `2`: TLS with CA certificate
- `3`: TLS with client certificate
- `4`: TLS with both CA and client certificates

Current default configuration uses:

- `MQTT_SECURITY_LEVEL = 1`
- `MQTT_HOST_PORT = 8883`

This means the demo is configured for **MQTT over TLS**.

#### RTP H.264 streaming

The encoded H.264 stream is sent over RTP once Wi-Fi is connected.

Default destination:

- Multicast IP: `239.0.0.0`
- RTP port: `RTP_PORT`
- RTCP port: `RTP_PORT + 1`

#### Splash screen on pause

A splash H.264 frame is embedded in the application and copied at runtime to PSRAM.

When the RTP stream is paused:

- live camera encoding is paused
- queued encoded frames are flushed
- a static splash H.264 frame is sent periodically at a low refresh rate

When the RTP stream is resumed:

- camera encoding resumes
- the next encoded frame is forced as an intra frame
- live video transmission continues

This provides a visible **play/pause screen behavior** instead of freezing on the last live frame.



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
- For Multicast streaming, IGMP should be enabled in the router options
- MQTT publish traffic may be postponed briefly during large RTP transmission bursts
- The included splash frame is static and pre-encoded

### __Build and flash__

- Build the `FSBL` firmware project using STM32CubeIDE. The resulting binary will be generated at:
    ```
    \STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.bin
    ```
- Resort to the [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) to add a header to the binary
  with the following command:
    ```shell
    STM32_SigningTool_CLI.exe -bin \ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.bin -nk -of 0x80000000 -t fsbl -o \ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.trusted.bin -hv 2.3 -align -dump Firmware\STM32N6570-DK\ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.trusted.bin
    ```
  The resulting binary is:
    ```
    \ST67W6X_RTP\STM32CubeIDE\FSBL\Debug\ST67W6X_RTP_FSBL.trusted.bin
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
  BLE advertising is started. To commission WiFi:
  - go to: https://applible.github.io/Web_Bluetooth_App_ST67/
  - or use the ST BLE Toolbox App on iOS/Android
  ...
  ```
   The ST67 is correctly advertising and it is ready for the Wi-Fi commissioning over BLE. 
  
  There are two options to commission Wi-Fi over BLE:
  
  - using the WebApp
  - using the iOS/Android app ST BLE Toolbox
  
   
  
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
  MQTT Connect successful
  Subscribing to topic /devices/mySTM32_772/control.
  MQTT Publish OK
  STA IP :
  IP :              10.0.0.37
  Gateway :         10.0.0.1
  Netmask :         255.255.255.0
   -> BLE NOTIFICATION DISABLED [Service: 0, Charac: 2].
   -> BLE NOTIFICATION DISABLED [Service: 0, Charac: 3].
   -> BLE DISCONNECTED [Conn_Handle: 0]
  ...
  ```

  After Wi-Fi connection:

  - BLE monitoring notifies the connected SSID
  - MQTT starts automatically
  - RTP streaming starts automatically on the multicast IP address 239.0.0.0



### MQTT over TLS details

###### Default MQTT settings

| Setting        | Value            |
| -------------- | ---------------- |
| Host           | `broker.emqx.io` |
| Port           | `8883`           |
| Client ID      | `mySTM32_772`    |
| Security level | `1`              |
| SNI            | `broker.emqx.io` |
| Keep alive     | `120`            |

###### Published telemetry

The device periodically publishes telemetry every 5 seconds with payload:

```
{
  "state": {
    "reported": {
      "time": "26-05-14 16:00:00",
      "mac": "40:82:7b:00:12:28",
      "rssi": -65
    }
  }
}
```

The publish topic is:

```
/sensors/<MQTT_CLIENT_ID>
```

With default configuration:

```
/sensors/mySTM32_772
```

###### Subscribed control topic

The application subscribes to:

```
/devices/<MQTT_CLIENT_ID>/control
```

With default configuration:

```
/devices/mySTM32_772/control
```

#### **Supported incoming JSON fields**

The subscription handler currently parses and logs or applies the following fields:

- `time`
- `rssi`
- `mac`
- `temperature`
- `pressure`
- `humidity`
- `LedOn`
- `Reboot`

Examples:

##### Turn the red LED on

```
{
  "LedOn": true
}
```

##### Reboot the board

```
{
  "Reboot": true
}
```



## Start the RTP stream

Start RTP playback on `ffplay` using the included SDP file.

```shell
ffplay -fflags nobuffer -flags low_delay -protocol_whitelist file,udp,rtp -i rtp.n6dk.sdp
```

The Streaming should start as shown below:

<img src="Images\macOsStreaming.png" alt="ffplay on MacOS" style="zoom:90%;" />

<img src="Images\LinuxStreaming.png" alt="ffplay on Linux" style="zoom:90%;" />

## **Pause / resume behavior and splash screen**

The **USER button** can now be used to pause and resume the RTP stream once:

- Wi-Fi is connected
- the RTP task is running

###### **When pause is requested**

- the RTP application sets the stream into paused state
- the camera encode path is paused
- already queued encoded frames are flushed
- instead of live video, a static embedded H.264 splash frame is transmitted at a low rate

###### When resume is requested

- the camera encode path resumes
- the encoder forces an intra frame after resume
- live RTP video restarts

###### **Why the splash frame is used**

The splash frame improves pause behavior because:

- the receiver continues to get valid H.264/RTP data
- the screen does not remain on a stale live frame
- resume becomes visually explicit to the user

##### Button behavior summary

| State                               | USER button action                 |
| ----------------------------------- | ---------------------------------- |
| No Wi-Fi stream active              | BLE disconnect / unpair management |
| Wi-Fi connected and RTP task active | Toggle RTP pause / resume          |

## __Troubleshooting__

### **MQTT does not connect**

Check the following:

- the board has valid DNS servers
- outbound access to port 8883 is allowed
- broker hostname is reachable
- TLS settings match the selected security level
- system time is valid if certificate verification is enabled in your setup

Useful log lines include:

```
DNS resolved immediately: broker.emqx.io -> ...
MQTT Connect successful
MQTT Publish OK
```

If you see:

```
DNS Lookup timed out
ssl_configure failed
ssl_secure_connection failed
MQTT Connect failed: ...
```

then verify network reachability and TLS configuration.

### No reaction to subscribed MQTT commands

Verify that:

- you publish to the correct topic:

  ```
  /devices/mySTM32_772/control
  ```

- the JSON is valid

- the board is already connected to MQTT

- the command field is supported by the parser

### **Pause works but live stream does not resume correctly**

Check for:

- receiver still listening to the same SDP / multicast stream

- button debouncing and repeated presses

- logs showing:

  ```
  Camera encode path resumed
  RTP resume requested
  ```
