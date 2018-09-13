/*
This program was written by Dean McGee and is free for any use

Program to support multifunction GPS-based speedometer
  using the following hardware:
    * WeMos D1 Mini microcontroller
    * GPS module
    * Nextion NX2432T024 HMI display
    * DHT22 temperature/humidity sensor
    * Optional 128x64 IIC oled display
    * Jumper wires

Displays on a Nextion display the following Data
  Speed
  Heading
  Current time
  Date
  Temperature
  Humidity
  Heat index

Library Dependencies:
lib_deps =
lib_deps =
  Nextion                  ; by ITEAD Studio
  ESP8266_SSD1306          ; by Fabrice Weinberg & Daniel Eichhorn
  Adafruit Unified Sensor  ; by Adafruit
  DHT sensor library       ; by Adafruit
  TinyGPSPlus              ; by Mikal Hart

Required changes for Nextion:
  NexConfig.h:
    Comment out DEBUG_SERIAL_ENABLE, we're not using Nextion debug 
    Only Serial1 TX pin is connected, we can't recieve anything from Nextion
    ESP8266 Serial1 RX is not available, it is used used internally by WeMos
  NexHardware.h:
    change timeout values to zero otherwise display will be very slow

D1 GPIO5 SCL -- OLED
D2 GPIO4 SDA -- OLED
D3           -- DHT
D4           -- NexTion Serial1 Tx
TX           -- GPS
RX           -- GPS
*/

/*
Change log:
20180913 hdm initial code
*/

// A separate OLED display is useful for debugging
#define USE_OLED 1

#include <Arduino.h>
#include <Wire.h>

#if USE_OLED
 #include "SH1106.h" // for 1.3" OLED from ESP8266_SSD1306_ID562
#endif

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <TinyGPS++.h>
#include "ArduinoOTA.h"
#include "Nextion.h"
#include "DHT.h" 

// define WIFI_SSID & WIFI_PASSPHRASE
// #define WIFI_SSID "my_wifi_ssid"
// #define WIFI_PASSPHRASE "my_passphrase"
#include "secrets.h" 

#define DHTTYPE DHT22
#define DHTPIN  D3

// DHT22 Temperature and Humidity sensor related variables
DHT    dht(DHTPIN,DHTTYPE);
float  humidity    = 0;
float  temperature = 0;
float  RH          = 0;
float  T           = 0;
float  HI          = 0;
ulong  DHTLastRead = 0;

// Oled display for debug
#if USE_OLED
SH1106 display(0x3c, D2, D1);
#endif
// SCL GPIO5  D1
// SDA GPIO4  D2

// The TinyGPS++ object and related variables
TinyGPSPlus gps;
static const uint32_t GPSBaud = 9600;
float  gpsLatitude = 0;
float  gpsLongitude = 0;
float  gpsSpeed = 0;
float  gpsCourse = 0;

String gpsMonth = "     ";
String gpsDay =   "     ";
String gpsYear =  "     ";
String strHour =   "      ";
String strMinute = "      ";
String strSecond = "     ";
String gpsHeading = "      ";

int    gpsHour = 0;
int    gpsMinute = 0;
int    gpsSecond = 0;
int    speedInt;
int    speedTenth;



// Nextion main page display, page0
NexText t1 =  NexText(0,26,"t1");  // Temerature
NexText t2 =  NexText(0,1, "t2");  // Humidity
NexText t5 =  NexText(0,4, "t5");  // Heat index
NexText t13 = NexText(0,27,"t13"); // first two digits of speed
NexText t16 = NexText(0,14,"t16"); // first decimal digit of speed
NexText t18 = NexText(0,17,"t18"); // Month
NexText t19 = NexText(0,18,"t19"); // Day
NexText t20 = NexText(0,19,"t20"); // Year
NexText t23 = NexText(0,22,"t23"); // Hour
NexText t24 = NexText(0,23,"t24"); // Minute
NexText t25 = NexText(0,24,"t25"); // Second
NexText t26 = NexText(0,25,"t26"); // Heading
NexText t29 = NexText(0,28,"t29"); // 100ms tick counter

// Nextion second page for debug, page1
NexText tb1 =  NexText(0,1, "tb1");
NexText tb2 =  NexText(0,2, "tb2");
NexText tb3 =  NexText(0,3, "tb3");
NexText tb4 =  NexText(0,4, "tb4");
NexText tb5 =  NexText(0,5, "tb5");
NexText tb6 =  NexText(0,6, "tb6");
NexText tb7 =  NexText(0,7, "tb7");
NexText tb8 =  NexText(0,8, "tb8");
NexText tb9 =  NexText(0,9, "tb9");
NexText tb10 = NexText(0,10,"tb10");


NexPage page0 = NexPage(0,0, "page0"); // main page
NexPage page1 = NexPage(1,0, "page1"); // debug page

// buffer for storing text to send to Nextion
char buffer[50] = {0};

// debug related variables
String wifiIP        = "000.000.000.000";
ulong  nxDHTTime     = 0;
ulong  nxDateTime    = 0;
ulong  nxTimeTime    = 0;
ulong  nxSpeedTime   = 0;
ulong  nxHeadingTime = 0;
ulong  loopStart     = 0;
ulong  page0Time     = 0;
ulong  page1Time     = 0;
ulong  DHTTime       = 0;
ulong  GPSTime       = 0;
ulong  oledTime      = 0;
ulong  otaTime       = 0;
ulong  loopTime      = 0;

void setDbText(NexText Tx, const char* text)
{
  strncpy(buffer,text,sizeof(buffer));
  Tx.setText(buffer);
} // setDbText

void setNexText(NexText Tx, String *nexString)
{
  memset(buffer, 0, sizeof(buffer));
  for (uint i = 0; i< nexString[0].length(); i++) {
    buffer[i] = nexString[0][i];
  }
  Tx.setText(buffer);
} // setNexText

void readGPS()
{

  while (Serial.available() > 0) {
    GPSTime = millis();

    if (gps.encode(Serial.read())){
      if (gps.location.isValid()) {
        gpsLatitude = (gps.location.lat());
        gpsLongitude = (gps.location.lng());
      } // if location
      if (gps.date.isValid()) {
        
        gpsMonth = String(gps.date.month());
        gpsDay = String(gps.date.day());
        gpsYear = String(gps.date.year());
      } // if date
      if (gps.time.isValid()) {
        gpsHour = gps.time.hour() - 4;
        gpsMinute = gps.time.minute();
        gpsSecond = gps.time.second();
      } // if time
      if (gps.speed.isValid()) {
        gpsSpeed = gps.speed.mph();
        if (gpsSpeed < 1.0) gpsSpeed = 0;
      } // if speed
      if (gps.course.isValid()) {
        gpsCourse = gps.course.deg();
      } // if course
    } // if gps.encode
    GPSTime = millis() - GPSTime;
  } // while
} // readGPS

void readDHT() 
{
  if ((millis()-DHTLastRead)>2000) {
    DHTTime = millis();
    humidity = dht.readHumidity();
    temperature = dht.readTemperature(true);
    if (!isnan(humidity) && !isnan(temperature)) {
      RH = humidity;
      T = temperature;
      HI = dht.computeHeatIndex(T,RH);
    }
    DHTLastRead = millis();
    DHTTime = DHTLastRead - DHTTime;
  }
} // readDHT

#if USE_OLED
void updateOLED()
{
  long oledStart;
  oledStart = millis();
  display.clear();
  display.drawString(0,0,"IP: " + wifiIP + " " + String(loopTime/100));
  display.drawString(0,10,strHour + ":" + strMinute + ":" + strSecond + " " + String(millis()));
  display.drawString(0,20,"GPS Time: " + String(GPSTime));
  display.drawString(0,30,"DHT Time: " + String(DHTTime));
  display.drawString(0,40,"Oled Time: " + String(oledTime));
  display.drawString(0,50,"Page0: " + String(page0Time) + ", Page1: " + String(page1Time));

  display.display();
  oledTime = millis() - oledStart;
} // updateOLED

void initOLED()
{
  // Initialising the UI will init the display too.
  display.init();

  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  // The coordinates define the left starting point of the text
  display.setTextAlignment(TEXT_ALIGN_LEFT);
} // initOLED
#endif

void updatePage0Nextion()
{
String str = "       ";

  // update temperature & humidity fields
  page0Time = millis();
  nxDHTTime = millis();
  itoa(T,buffer,10);
  t1.setText(buffer);

  sprintf(buffer,"%1.0f%%",RH);
  t2.setText(buffer);

  itoa(HI,buffer,10);
  t5.setText(buffer);
  nxDHTTime = millis() - nxDHTTime;
  nxDateTime = millis();

  // update date field
  setNexText(t18, &gpsMonth);
  setNexText(t19, &gpsDay);
  setNexText(t20, &gpsYear);
  nxDateTime = millis()-nxDateTime;

  // update time field
  nxTimeTime = millis();
  if (gpsHour < 0) gpsHour = gpsHour + 12;
  if (gpsHour > 12) gpsHour = gpsHour - 12;
  strHour = String(gpsHour) + ":";
  if (gpsMinute < 10) strMinute = "0" + String(gpsMinute);
  else strMinute = String(gpsMinute);
  if (gpsSecond < 10) strSecond = "0" + String(gpsSecond);
  else strSecond = String(gpsSecond);
  strHour = String(gpsHour);
  setNexText(t23, &strHour);
  setNexText(t24, &strMinute);
  setNexText(t25, &strSecond);
  nxTimeTime = millis() - nxTimeTime;

  // update speed field
  nxSpeedTime = millis();
  speedInt = gpsSpeed;
  gpsSpeed = (gpsSpeed-float(speedInt))*10.0;
  speedTenth = gpsSpeed; 

  itoa(speedInt,buffer,10);
  t13.setText(buffer);
  itoa(speedTenth,buffer,10);
  t16.setText(buffer);
  nxSpeedTime = millis() - nxSpeedTime;

  t26.setText(TinyGPSPlus::cardinal(gpsCourse));

  nxHeadingTime = millis() - nxHeadingTime;

  // update 100ms tick field
  itoa(millis()/100,buffer,10); 
  t29.setText(buffer);
  page0Time = millis() - page0Time;

} // updatePage0Nextion

void updatePage1Nextion()
{
  String str;
  long page1Start;
  page1Start = millis();
  setNexText(tb1,&wifiIP);
  tb2.setText(WIFI_SSID);
  sprintf(buffer,"Page0 time: %lu", page0Time);
  tb3.setText(buffer);
  sprintf(buffer,"Page1 time: %lu",page1Time);
  tb4.setText(buffer);
  sprintf(buffer,"DHT time: %lu, GPS time: %lu",DHTTime,GPSTime);
  tb5.setText(buffer);
  sprintf(buffer, "OLED time: %lu, Loop time %lu", oledTime, loopTime);
  tb6.setText(buffer);
  sprintf(buffer,"DHT: %lu, Date: %lu", nxDHTTime, nxDateTime);
  tb7.setText(buffer);
  sprintf(buffer,"Time: %lu, Speed: %lu", nxTimeTime, nxSpeedTime);
  tb8.setText(buffer);
  sprintf(buffer,"Heading: %lu",nxHeadingTime);
  tb9.setText(buffer);
  page1Time = millis() - page1Start;
} // updatePage1Nextion

void setup() 
{
  int wifiTries = 0;

#if USE_OLED
  initOLED();
#endif

  nexInit();
  delay(200);
  page1.show();

#if USE_OLED
  display.clear();
  display.drawString(0, 10, "Starting DHT");
  display.display();
#endif

  setDbText(tb1,"Starting DHT");
  dht.begin();
  delay(2000);

#if USE_OLED
  display.clear();
  display.drawString(0, 10, "Starting GPS");
  display.display();
#endif

  setDbText(tb2,"Starting GPS");
  Serial.begin(GPSBaud);


  // wifi is only used for OTA
  // give up if can't connect in 100 tries
  WiFi.begin(WIFI_SSID, WIFI_PASSPHRASE);
  setDbText(tb3, "Starting WiFI");
  while ((WiFi.status() != WL_CONNECTED) && (wifiTries < 100)) {
    setDbText(tb4, itoa(wifiTries, buffer,10));
    wifiTries++;
    delay(100);
  }

  // create a string to display IP
  wifiIP = String(WiFi.localIP()[0]) + "." + String(WiFi.localIP()[1]) + "." + 
                        String(WiFi.localIP()[2]) + "." + String(WiFi.localIP()[3]);

  // setup Over The Air updating
  ArduinoOTA.begin();
  delay(100);    
  page0.show();
} // setup

void loop() {
long startTime;
  loopStart = millis();
  startTime = loopStart;

#if USE_OLED  
  updateOLED();
#endif

  readDHT();
  if (Serial.available()) {
    readGPS();
    GPSTime = millis() - startTime;
  }

  updatePage1Nextion();
  updatePage0Nextion();
  startTime = millis();

  // Check for Over The Air update
  ArduinoOTA.handle();

  otaTime = millis() - startTime;
  loopTime = millis() - loopStart;
  delay(10);
} // loop