If you like this work, please consider [sponsoring this project!](https://paypal.me/cotesta)

# async-esp-fs-webserver
ESP32/ESP8266 WebServer, WiFi manager and ACE web editor Arduino library. Based on [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) from @me-no-dev

Thi is the equivalent to **esp-fs-webserver** Arduino library, but working with the very good **ESPAsyncWebServer** library instead default webserver library.
From some tests I have done, I have been able to observe that a webserver created with this library is much faster and more responsive, which is why I decided to develop this variant of the library.

**Note**:
Starting from version 2.0.0 ESP32 core for Aruino introduced the LittlsFS library like ESP8266. The examples in this library is written to work with this for both platform by default. Change according to your needs if you prefer other filesystems.

## WiFi, OTA firmware update and Options manager
Thanks to the built-in page **/setup** (about 6.9Kb of program space) is possible scan and set the WiFi credentials and other freely configurable parameters.

With **/setup** webpage is also possible perform remote firmware update. 

![image](https://github.com/cotestatnt/async-esp-fs-webserver/assets/27758688/e16cf5cb-252a-42bb-b111-305387f9d0cc)


This web page can be injected also with custom HTML and Javascript code in order to create very smart and powerful web application.

In the image below, for example, the HTML and Javascript code to provision the devices in the well-known [ThingsBoard IoT platform](https://thingsboard.io/) has been added at runtime starting from the Arduino sketch (check example [customHTML.ino](https://github.com/cotestatnt/async-esp-fs-webserver/tree/main/examples/customHTML)).

![image](https://github.com/cotestatnt/async-esp-fs-webserver/assets/27758688/74c3046b-fc04-46d6-86ad-acb68a03f38e)


## ACE web file editor/browser
Thanks to the built-in **/edit** page, is possible upload, delete and edit the HTML/CSS/JavaScript source files directly from browser and immediately displaying the changes introduced at runtime without having to recompile the device firmware.
The page can be enabled at runtime using the method `enableFsCodeEditor()` and it occupy about 6.7Kb of program space.

![image](https://github.com/cotestatnt/async-esp-fs-webserver/assets/27758688/668c0899-a060-4aed-956b-51311bf3fe13)

