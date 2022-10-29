# esp-idf-video-snapshot
Capture still images from a USB camera using ESP-IDF.   

This example demonstrates how to:   
- Capture still images from a USB camera using the USB Host UVC Dreiver.
 https://components.espressif.com/components/espressif/usb_host_uvc   
- View still images over WiFi using the built-in HTTP server.   
- Post still images over WiFi using MQTT/HTTP.   
- Supports plugging and unplugging USB cameras.   

This example enumerates the attached camera descriptors, negotiates the selected resolution and FPS, and starts capturing video.   

I based it on [this](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/uvc) official example.   

# Hardware Required

## ESP32-S2/ESP32-S3
This example requires any ESP32-S2 or ESP32-S3 with external PSRAM and exposed USB connector attached to USB camera.   
ESP module without external PSRAM will fail to initialize.   
I used this board.   
![esp32-s3-1](https://user-images.githubusercontent.com/6020549/193182915-ac4dbd55-b3ee-4327-b64d-d055e3774b7d.JPG)

## Stable power supply
USB cameras consume a lot of electricity.   
If the power supplied to the USB port is insufficient, the camera will not operate and ESP32 resets.  

## USB Type-A Femail connector
Breakout boards are available from AliExpress or eBay.   

## USB camera with UVC support
[Here](https://www.freebsd.org/cgi/man.cgi?query=uvc&sektion=4&manpath=freebsd-release-ports) is a list of USB cameras that support UVC that work with FreeBSD.   
From what I've researched, ESP-IDF has limited USB support.   
For example, the Logitech C615 works with Linux, but not with ESP-IDF.   
I tested with these cameras.   
- Logitech C270 -> Success   
- Logitech C615 -> Fail  
- Logitech QuickCam Pro 9000 -> Fail   
- PAPALOOK AF925 -> Fail   
- Microdia(Very old model) -> Success   
- Microdia MSI Starcam Racer -> Success   
- Microsoft LifeCam NX6000 -> Fail   
- Microsoft LifeCam Cinema -> Success   
- Microsoft LifeCam Studio -> Success   
- Microsoft LifeCam HD3000 -> Success   
- Microsoft LifeCam HD5000 -> Success   

___It is very hard to find a camera that works with ESP-IDF.___   
On [this](https://components.espressif.com/components/espressif/usb_host_uvc) document says that these cameras work with ESP-IDF.   
- Logitech C980
- CANYON CNE-CWC2

When usb support provided by ESP-IDF is updated, this issue may eliminate the problem.   
Detail is [here](https://github.com/nopnop2002/esp-idf-video-streaming/issues).   

![cameras](https://user-images.githubusercontent.com/6020549/195963953-2fd44723-1ef6-4ece-84c3-412f9ca1497c.JPG)


# Software Required
esp-idf v5.0 or later.   
A compilation error occurs in ESP-IDF Ver4.   

# Wireing   
```
ESP BOARD          USB CONNECTOR (type A)
                         +--+
5V        -------------> | || VCC
[GPIO19]  -------------> | || D-
[GPIO20]  -------------> | || D+
GND       -------------> | || GND
                         +--+
```


# Installation
```
git clone https://github.com/nopnop2002/esp-idf-video-snapshot
cd esp-idf-video-snapshot
idf.py set-target {esp32s2/esp32s3}
idf.py menuconfig
idf.py flash monitor
```
# Configuration

![config-top](https://user-images.githubusercontent.com/6020549/198750978-457056f7-1215-4df2-afab-1d5b6dbbcdeb.jpg)
![config-app](https://user-images.githubusercontent.com/6020549/198750980-bd055f0b-6e43-4237-b92e-75bdb6cf327f.jpg)

## Wifi Setting

![config-wifi-1](https://user-images.githubusercontent.com/6020549/198751049-a289342f-2fa9-4a33-87f4-5fb113996b2f.jpg)

You can connect using the mDNS hostname instead of the IP address.   
![config-wifi-2](https://user-images.githubusercontent.com/6020549/198751091-3bd33bc0-e84d-4342-8772-d95db06206a7.jpg)

You can use static IP.   
![config-wifi-3](https://user-images.githubusercontent.com/6020549/198751103-27d34e58-6e99-4f5b-93a0-cebcbea8e7a7.jpg)


## Select Snapshot Trigger

You can choose one of the following as trigger

- Trigger is the Enter key on the keyboard   
For operation check

![config-trigger-1](https://user-images.githubusercontent.com/6020549/198751298-e109b555-edf2-4d59-8386-14fd59951aeb.jpg)

- Trigger is a GPIO toggle

  - Initial Sate is PULLDOWN   
The trigger is prepared when it is turned from OFF to ON, and a picture is taken when it is turned from ON to OFF.   

  - Initial Sate is PULLUP   
The trigger is prepared when it is turned from ON to OFF, and a picture is taken when it is turned from OFF to ON.   

![config-trigger-2](https://user-images.githubusercontent.com/6020549/198751319-6fe917cf-a63a-4c70-ad8f-bda4ff8813c5.jpg)

- Trigger is TCP Socket   
You can use tcp_send.py as trigger.   
`python3 ./tcp_send.py`

![config-trigger-3](https://user-images.githubusercontent.com/6020549/198751416-bf5ac971-37af-4fb3-a40a-c87c9023905c.jpg)

- Trigger is UDP Socket   
You can use udp_send.py as trigger.   
```
python3 -m pip install -U netifaces
python3 ./udp_send.py
```

![config-trigger-4](https://user-images.githubusercontent.com/6020549/198751480-7784dfba-9d23-4c2f-a744-15b63165a027.jpg)

- Trigger is MQTT Subscribe   
You can use mosquitto_pub as trigger.   
`mosquitto_pub -h your_broker -p 1883 -t "/topic/picture/sub" -m ""`

![config-trigger-5](https://user-images.githubusercontent.com/6020549/198751524-7bdbcbcd-ca89-4cdf-ae76-4d920e1eb92c.jpg)


- Trigger is HTTP Request   
You can use this command as trigger.   
`curl "http://esp32-camera.local:8080/take/picture"`

![config-trigger-6](https://user-images.githubusercontent.com/6020549/198751674-a8c46f86-f5d6-43e4-8d49-0f733d92a882.jpg)


## Post Setting

- No post   
Still images don't post anywhere.   
![config-post-1](https://user-images.githubusercontent.com/6020549/198752042-278e8234-0031-44be-ab7d-371e88de018c.jpg)

- Post using HTTP   
Still images are posted using HTTP.   
You can download the HTTP server from [here](https://github.com/nopnop2002/multipart-upload-server).   

![config-post-21](https://user-images.githubusercontent.com/6020549/198752075-4fd95b75-b1dc-432e-957c-2204fe6d5db9.jpg)
![config-post-22](https://user-images.githubusercontent.com/6020549/198752078-16e2dfb9-357e-4600-9fc5-c13373753861.jpg)


- Post using MQTT   
Still images are posted using MQTT.   
You can use subscribe.py as viewer.   
```
python3 -m pip install -U wheel
python3 -m pip install paho-mqtt
python3 -m pip install opencv-python
python3 -m pip install numpy
python3 ./subscribe.py
```

![config-post-3](https://user-images.githubusercontent.com/6020549/198752572-26338565-fe6e-4efa-b9ae-bd85fb8131bf.jpg)


## Camera Setting
Some cameras need to change frame size and frame format.   
See [here](https://github.com/nopnop2002/esp-idf-video-streaming/issues).   
![config-camera-1](https://user-images.githubusercontent.com/6020549/198752628-4920fb61-2789-4e59-890e-8783ac16f7ac.jpg)

![config-camera-2](https://user-images.githubusercontent.com/6020549/198752640-83012d4d-6102-48a1-a24b-3d591fc0595d.jpg)

![config-camera-3](https://user-images.githubusercontent.com/6020549/198752641-df0271a9-4c85-40b1-a169-1702608c4a4a.jpg)



# How to use

- Build and flash firmware to esp32.

- Connect the USB camera at this timing.   
![WaitingForDevice](https://user-images.githubusercontent.com/6020549/193183218-1a2100c8-7b51-444d-953e-cf7b8acd3313.jpg)

- For available USB cameras, device information will be displayed and video streaming will begin.   
![StartStreaming](https://user-images.githubusercontent.com/6020549/193183252-fa3473ef-b0b4-4639-b01f-e7ce8f497583.jpg)

- For unavailable USB cameras, you will see an error like this.   
![NotSupportCamera](https://user-images.githubusercontent.com/6020549/193183435-e35e03e4-e5f7-4efd-bfbf-87e2afde3b6f.jpg)

- Execute trigger.  

# View picture using Built-in WEB Server
You can view the pictures taken using the built-in WEB server.   
Enter the ESP32's IP address and port number in the address bar of your browser.   
You can use mDNS instead of IP address.   

![browser](https://user-images.githubusercontent.com/6020549/124227364-837a7880-db45-11eb-9d8b-fa15c676adac.jpg)



