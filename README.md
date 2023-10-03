If you like this work, please consider [sponsoring this project!](https://paypal.me/cotesta)

# async-esp-fs-webserver
ESP32/ESP8266 WebServer, WiFi manager and ACE web editor Arduino library. Based on [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) from @me-no-dev

Thi is the equivalent to **esp-fs-webserver** Arduino library, but working with the very good ESPAsyncWebServer library instead default webserver library.
From some tests I have done, I have been able to observe that a webserver created with this library is much faster and more responsive, which is why I decided to develop this variant of the library.

**Note**:
Starting from version 2.0.0 ESP32 core for Aruino introduced the LittlsFS library like ESP8266. The examples in this library is written to work with this for both platform by default. Change according to your needs if you prefer other filesystems.

## ACE web broswer editor
Thanks to the built-in **/edit** page (enabled by default), is possible upload, delete and edit the HTML/CSS/JavaScript source files directly from browser and immediately displaying the changes introduced at runtime without having to recompile the device firmware.

![editor](https://user-images.githubusercontent.com/27758688/122570105-b6a01080-d04b-11eb-832c-f60c0a886efd.png)



## WiFi manager
I've added also another built-in page **/setup**, also enabled by default, with which it is possible to set the WiFi credentials and other freely configurable parameters.
By **/setup** is also possible perform remote firmware update. 

![image](https://user-images.githubusercontent.com/27758688/218732999-e22fe092-cbc9-40d6-a34b-38282fbd60e2.png)

This web page can be injected also with custom HTML and Javascript code in order to create very smart and powerful web application.

In the image below, for example, the HTML and Javascript code to provision the devices in the well-known [ThingsBoard IoT platform](https://thingsboard.io/) has been added at runtime starting from the Arduino sketch (check example customHTML.ino).

![image](https://user-images.githubusercontent.com/27758688/218733394-9cd7af3e-257e-4798-98b0-b8d426e07848.png)



