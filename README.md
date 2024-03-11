If you like this work, please consider [sponsoring this project!](https://paypal.me/cotesta)

# async-esp-fs-webserver
ESP32/ESP8266 WebServer, WiFi manager and ACE web editor Arduino library. Based on [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) from @me-no-dev

This is the equivalent to [**esp-fs-webserver**](https://github.com/cotestatnt/esp-fs-webserver/) Arduino library, but working with the very good **ESPAsyncWebServer** library instead default webserver library.

**Note**:
Starting from version 2.0.0 ESP32 core for Arduino introduced the LittlsFS library like ESP8266. The examples in this library is written to work with this for both platform by default. Change according to your needs if you prefer other filesystems.

## WiFi, OTA firmware update and Options manager
Thanks to the built-in page **/setup** (about 8Kb of program space) it is possible to scan and set the WiFi credentials and other freely configurable parameters.

With **/setup** webpage it is also possible to perform remote firmware update (OTA-update). 

![image](https://github.com/cotestatnt/async-esp-fs-webserver/assets/27758688/81a1f6db-a4bd-4f1d-b263-7bebe79cae7d)


This web page can be injected also with custom HTML and Javascript code in order to create very smart and powerful web application.

In the image below, for example, the HTML and Javascript code to provision the devices in the well-known [ThingsBoard IoT platform](https://thingsboard.io/) has been added at runtime starting from the Arduino sketch (check example [customHTML.ino](https://github.com/cotestatnt/async-esp-fs-webserver/tree/main/examples/customHTML)).

![image](https://github.com/cotestatnt/async-esp-fs-webserver/assets/27758688/d728c315-7271-454d-8c34-fb9db0b7a333)

## Web server file upload

In addition to built-in firmware update functionality, you can also upload your web server content all at once (typically the files are placed inside the folder `data` of your sketch).

![image](https://github.com/cotestatnt/async-esp-fs-webserver/assets/27758688/7c261216-3acd-4463-9105-d11e0be3a59a)



## ACE web file editor/browser
Thanks to the built-in **/edit** page, it is possible to upload, delete and edit the HTML/CSS/JavaScript source files directly from browser and immediately display the changes introduced at runtime without having to recompile the device firmware.
The page can be enabled at runtime using the method `enableFsCodeEditor()` and it occupies about 6.7Kb of program space.

![image](https://github.com/cotestatnt/async-esp-fs-webserver/assets/27758688/668c0899-a060-4aed-956b-51311bf3fe13)

