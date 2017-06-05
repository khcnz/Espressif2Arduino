# Espressif2Arduino
Used for bootstrapping the replacement of the default Espressif ESP8266 bootloader, replacing it with the Arduino bootloader and a custom rom.  

This was designed specifically to replace the stock sonoff itead firmware by intercepting firmware update requests and serving these binary images. More info on how to intercept and stream this firmware to a device is [here](http://blog.nanl.de/2017/05/sonota-flashing-itead-sonoff-devices-via-original-ota-mechanism/) and [here](https://github.com/mirko/SonOTA) 

The process works like this:

1. If Rom 1 is requested by the OTA update process Rom 1 is streamed back to the Espressif SDK which will flash  to 0x1000 and reboot into this image. This image only has one job which is to simply request the Rom 2 image, save it into flash at 0x81000 and reboot into it.
2. If Rom 2 is requested by the OTA update process Rom 2 is streamed back to the Espressif SDK and will be saved into flash at 0x81000 and will then reboot.
3. Now both irrespective of route taken we will now be running Rom 2 at 0x81000. This allows us to replace all contents at 0x0->0x7FFF which includes the bootloader and the booted arduino image. Rom 2 simply requests the final image, saves it to 0x0 and then restarts.  


Current Limitations:

- Currently only supports the ESP8266 with 1MB of flash and QIO Flash mode. 
- To download the subsequent images WIFI is needed - currently this process tries to find the wifi credentials that  the itead firmware has stored - alternatively hard-code your SSID and password.
- Could do with smarter logic before blowing away the bootloader on slot 1. Ideally it would only do this at the end of the process assuming everything flashed correctly (and checksums match). Currently it blows these away as soon as a valid HTTP response code and magic byte is received.

This is very much alpha quality at the moment, don't use this process on any devices you don't/can't manually flash if they don't boot.


# Prereqs
1. Arduino IDE with Arduino SDK 2.3.0
2. Python 2.7 / 3.4
3. ESP Tool (pip install esptool)


# Building 
1. From the Arduino Folder replace boards.txt and the additional ld scripts into the appropriate place for your instance of Arduino.
2. You must replace the url for the three downloads:
	1. Espressif OTA Rom 1 (http://x.x.x.x/Espressif2Arduino.ino-0x01000.bin)
	2. Espressif OTA Rom 2 (http://x.x.x.x/Espressif2Arduino.ino-0x81000.bin)
	3. Rom 3 - Your new Arduino Build (http://x.x.x.x/sonoff.bin) 
2. In Arduino IDE first select the Flash Size "1M (Espressif OTA Rom 1)" and build. 
3. From your build directory run the following command and save the resulting output
 
		esptool.py elf2image --version 2  Espressif2Arduino.ino.elf

4. Repeat for Flash Size "1M (Espressif OTA Rom 2)"

You should now have three files ready for serving over HTTP:

- Rom 1 (Espressif2Arduino.ino-0x01000.bin)
- Rom 2 (Espressif2Arduino.ino-0x81000.bin)
- Arduino Build 


# Flashing / Running
Although it is intended you would flash these images OTA you can simulate the process by directly flashing Espressif2Arduino onto the currently active Espressif rom slot.

1. First take a backup image of your flash by running 

		esptool.py -p COM5 read_flash 0x0 0x100000 backup-0x00000.bin (note this is slow ~2 mins)

2. If rom 1 is active
		
		esptool.py -p COM5 -b 921600 write_flash 0x01000 Espressif2Arduino.ino-0x01000.bin

3. If rom 2 is active
 
		esptool.py -p COM5 -b 921600 write_flash 0x01000 Espressif2Arduino.ino-0x81000.bin

4. To restore your backup

		esptool.py -p COM5 -b 921600 write_flash 0x0 backup-0x00000.bin

Note - there is a bug with the ESP8266 that it will stay in flash mode the first reboot after flashing by serial. Best thing to do is to flash, then immediately pull power when finished and reboot it - with this code as standard you have 5 seconds before it will try update so you can pull power in this window without an issue.

# Troubleshooting
GPIO 13 is used by default as a status indicator

- Slow flashing indicates waiting for 5 seconds at startup before completing actions (so you can connect a terminal)
- Slower Flashing indicates attempting to connecting to wifi
- Fast Flashing = Erasing/Flashing image

If you connect the serial port you should also see output like this:
	
	Current Rom: 2
	Rom 1 Magic Byte: 0xEA
	Flash Mode: 1

	Attempting to read itead Wifi credentials... Done
        SSID: xxx
        Password: xxx
	Connecting to Wifi..................................Done
        192.168.1.35
	Flash Rom 1: http://192.168.1.xx/sonoff.bin
	HTTP Response Code: 200
	HTTP Response Length: 483152
	Magic Byte from stream: 0xE9
	**CRITICAL SECTION START - DO NOT TURN OFF POWER OR INTERFERE WITH WIFI**
	Erasing Flash Sectors 0-128....................................Done
	Downloading Image in 1024 byte blocks..........................Done
	**CRITICAL SECTION END**

