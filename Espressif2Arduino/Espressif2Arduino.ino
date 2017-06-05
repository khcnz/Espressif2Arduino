#include <ESP8266WiFi.h>                    // MQTT, Ota, WifiManager
#include <ESP8266HTTPClient.h>              // MQTT, Ota

extern "C" uint8_t system_upgrade_userbin_check();
extern "C" void system_upgrade_flag_set(uint8 flag);
extern "C" void system_upgrade_reboot (void);

byte buffer[1024] = {0};
uint16_t buffer_max_size = sizeof(buffer);
byte _blink = 0;

void connectToWiFiBlocking();
uint8_t determineFlashMode();
void blink();
void flashRom1();
void flashRom2();
void downloadRomToFlash(byte rom, byte magic, uint32_t to_address, uint32_t end_address, uint16_t erase_sectior_from, uint16_t erase_sector_to, char * url, uint8_t retry_limit);

enum FlashMode
{
  MODE_UNKNOWN,
  MODE_FLASH_ROM1,
  MODE_FLASH_ROM2
};

#define MAGIC_V1 0xE9
#define MAGIC_V2 0xEA
#define UPGRADE_FLAG_START 0x01
#define UPGRADE_FLAG_FINISH 0x02


//USER EDITABLE VARABLES HERE
#define DELAY 100
#define STATUS_GPIO 13
#define RETRY 3

#define URL_ROM_2 "http://192.168.1.1/Espressif2Arduino.ino-0x81000.bin"
#define URL_ROM_3 "http://192.168.1.1/sonoff.bin" //or use http://sonoff.maddox.co.uk/tasmota/sonoff.ino.bin

//Uncomment to provide fixed credentials - otherwise will try to use credentials saved by sonoff device
//#define WIFI_SSID "TEST"
//#define WIFI_PASSWORD "PASSWORD"



void setup()
{
  Serial.begin(115200);
  Serial.print("\nInitalizing...");
  if(STATUS_GPIO)
  {
    pinMode(STATUS_GPIO, OUTPUT);
  }

  //blink our status LED while we wait for serial to come up
  for(int i=0;i<50;i++)
  {
    blink(); 
    delay(DELAY);
  }
  digitalWrite(STATUS_GPIO, HIGH);
  Serial.println("Done");

  uint8_t mode = determineFlashMode();
  if(mode == MODE_FLASH_ROM1 || MODE_FLASH_ROM2)
  {
      connectToWiFiBlocking();
      digitalWrite(STATUS_GPIO, LOW);
  }
  
  if (mode == MODE_FLASH_ROM1)
    flashRom1();
  else if(mode == MODE_FLASH_ROM2)
    flashRom2();
  else
    Serial.println("Flash Mode not recognized");
}



uint8_t determineFlashMode()
{
  Serial.printf("Current Rom: ");
  uint8_t rom = system_upgrade_userbin_check() + 1;
  Serial.printf("%d\n", rom);

  Serial.printf("Rom 1 Magic Byte: ");
  uint32_t rom_1_start_address = 0x001000;
  byte magic = 0;
  ESP.flashRead(rom_1_start_address, (uint32_t*)&magic, 1);
  Serial.printf("0x%02X\n", magic);

  Serial.printf("Flash Mode: ");
  uint8_t mode = MODE_UNKNOWN;
  if (rom == 1 && magic == MAGIC_V2)
    mode = MODE_FLASH_ROM2;
  else if (rom == 2 && magic == MAGIC_V2)
    mode = MODE_FLASH_ROM1;
  
  Serial.printf("%d\n", mode);
  return mode;
}

void connectToWiFiBlocking()
{
  char ssid[32] = {0};
  char pass[64] = {0};
  
  Serial.print("Attemping to read itead Wifi credentials... ");
  ESP.flashRead(0x79000, (uint32_t *) &ssid[0], sizeof(ssid));
  ESP.flashRead(0x79020, (uint32_t *) &pass[0], sizeof(pass));
  Serial.print("Done\n");
  Serial.printf("\tSSID: %s\n", ssid);
  Serial.printf("\tPassword: %s\n", pass);

  
  Serial.printf("Connecting to Wifi...",ssid,pass);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);

#ifdef WIFI_SSID
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
#ifndef WIFI_SSID
  WiFi.begin(ssid,pass);
#endif

  while (WiFi.status() != WL_CONNECTED)
  {
    blink();
    delay(DELAY*2);
  }
  Serial.print("Done\n");
  IPAddress ip = WiFi.localIP();
  Serial.printf("\t%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
}



void flashRom1()
{
  downloadRomToFlash(
    1,                //Rom 1
    0xE9,             //Standard Arduino Magic
    0x0,              //Write to 0x0 since we are replacing the bootloader
    0x80000,          //Stop before 0x80000
    0,                //Erase Sector from 0 to
    128,              //Sector 128 (not inclusive)
    URL_ROM_3,  //URL
    RETRY             //Retry Count
  );

  ESP.restart();
}

//Simply copy over all data in flash from rom 1 to rom 2 and reboot.
void flashRom2()
{

  system_upgrade_flag_set(UPGRADE_FLAG_START);
  downloadRomToFlash(
    2,                //Rom 1
    0xEA,             //V2 Espressif Magic
    0x81000,          //Write to 0x0 since we are replacing the bootloader
    0x100000,         //Stop before end of ram
    128,              //Erase Sector from 128
    256,              //Sector 256 (not inclusive)
    URL_ROM_2,        //URL
    RETRY             //Retry Count
  );
  system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
  system_upgrade_reboot();
}

void downloadRomToFlash(byte rom, byte magic, uint32_t to_address, uint32_t end_address, uint16_t erase_sectior_from, uint16_t erase_sector_to, char * url, uint8_t retry_limit)
{
  Serial.printf("Flash Rom %d: %s\n", rom, url);
  
  uint8_t retry_counter = 0;
  while(retry_counter < retry_limit)
  {
    HTTPClient http;
    http.begin(url);
    http.useHTTP10(true);
    http.setTimeout(8000);
    
    uint16_t httpCode = http.GET();
    Serial.printf("HTTP Response Code: %d\n", httpCode);
    if(httpCode == HTTP_CODE_OK)
    {
      uint32_t len = http.getSize();
      Serial.printf("HTTP Response Length: %d\n", len);

      byte buffer[1024] = { 0 };
      uint8_t header[4] = { 0 };

      WiFiClient* stream = http.getStreamPtr();
      stream->peekBytes(&header[0],4);
      Serial.printf("Magic Byte from stream: 0x%02X\n", header[0]);
      if(header[0] == magic)
      {
        Serial.printf("**CRITICAL SECTION START - DO NOT TURN OFF POWER OR INTERFERE WITH WIFI**\n");
        Serial.printf("Erasing Flash Sectors %d-%d", erase_sectior_from, erase_sector_to);
        for (uint16_t i = erase_sectior_from; i < erase_sector_to; i++)
        {
          ESP.flashEraseSector(i);
          blink();
        }  
        Serial.printf("Done\n");

        Serial.printf("Downloading Image in %d byte size blocks", sizeof(buffer));
        while(len > 0)
        {
          size_t size = stream->available();
          if(size) 
          {
            int c = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
            ESP.flashWrite(to_address, (uint32_t*)&buffer[0], c);
            to_address +=c; //increment next write address
            len -= c; //decremeant remaining bytes
          }
          blink();
        }
        http.end();
        Serial.println("Done");
        Serial.println("**CRITICAL SECTION END**");
        return;
      }
    }
    
    Serial.println("Error encountered.. sleeping and will try again");
    delay(5000); //try again in 5 seconds...
    retry_counter++;
  }
  Serial.println("Retry counter exceeded - giving up");
}


void blink()
{
  if(STATUS_GPIO)
  {
     if(_blink)
      digitalWrite(STATUS_GPIO, LOW);
    else
      digitalWrite(STATUS_GPIO, HIGH);
      
      _blink ^= 0xFF;
  }
  Serial.print(".");
  yield(); // reset watchdog
}

void loop()
{
  delay(100);
}


