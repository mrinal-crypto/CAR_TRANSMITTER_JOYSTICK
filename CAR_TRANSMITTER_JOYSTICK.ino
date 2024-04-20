#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <WiFiManager.h>
#include <FirebaseESP8266.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include "SPIFFS.h"
#include <FS.h>

#define DATA_PIN 32
#define NUM_LEDS 1
#define CHIPSET WS2812
#define BRIGHTNESS 50
#define COLOR_ORDER GRB
#define STATUS_LED 0
#define BOOT_BUTTON_PIN 0

#define JOYX 34
#define JOYY 35

#define HORN 4
#define GPS_POWER 18

CRGB leds[NUM_LEDS];
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

int signalQuality[] = { 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 98, 98, 98, 97, 97, 96, 96, 95, 95, 94, 93, 93, 92,
                        91, 90, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 76, 75, 74, 73, 71, 70, 69, 67, 66, 64,
                        63, 61, 60, 58, 56, 55, 53, 51, 50, 48, 46, 44, 42, 40, 38, 36, 34, 32, 30, 28, 26, 24, 22, 20,
                        17, 15, 13, 10, 8, 6, 3, 1, 1, 1, 1, 1, 1, 1, 1
                      };

static const unsigned char image_arrow_down_bits[] U8X8_PROGMEM = {0x04, 0x04, 0x04, 0x04, 0x15, 0x0e, 0x04};
static const unsigned char image_arrow_up_bits[] U8X8_PROGMEM = {0x04, 0x0e, 0x15, 0x04, 0x04, 0x04, 0x04};

const int portalOpenTime = 300000;  //server open for 5 mins
int navX, navX2;
int navY, navY2;
int originX, originY;
bool onDemand;
bool oriUploadExecuted = false;
String firebaseStatus = "";
String ssid = "";
String linkStatus = "";
String concatenatedDMSLat = "";
String concatenatedDMSLng = "";
float batteryLevel = 11.99;
float blc = 11.99;
float bhc = 12.5;
float latti = 00.00000;
float longi = 00.00000;
float carSpeed = 00.00;
uint8_t wifiRSSI = 0;
uint8_t potValue;

uint8_t hornValue;
uint8_t headlightValue;
uint8_t satNo;
uint8_t gpsValue;

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

AsyncWebServer server(80);
Preferences preferences;

TaskHandle_t Task1;


void setup() {
  Serial.begin(115200);
  u8g2.begin();
  delay(100);
  welcomeMsg();
  delay(3000);
  u8g2.clearDisplay();


  pinMode(BOOT_BUTTON_PIN, INPUT);
  pinMode(JOYX, INPUT);
  pinMode(JOYY, INPUT);
  pinMode(HORN, INPUT);
  pinMode(GPS_POWER, INPUT);
  delay(500);

  if (SPIFFS.begin(true)) {
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(0, 10, "FILESYSTEM = OK!");
    u8g2.sendBuffer();
  }
  if (!SPIFFS.begin(true)) {
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(0, 10, "FILESYSTEM = ERROR!");
    u8g2.sendBuffer();
  }
  delay(1000);

  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  delay(500);


  connectWiFi();
  delay(1000);
  connectFirebase();
  delay(2000);
  setOrigin(150);
  uploadOriginValues();
  delay(1000);
  u8g2.clearDisplay();
  drawLayout();
  delay(1000);

  xTaskCreatePinnedToCore(
    loop1,
    "Task1",
    10000,
    NULL,
    1,
    &Task1,
    1);
  delay(500);
}
////////////////////////////////////////////////////////////////////////
void welcomeMsg() {

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_luBIS18_tr);
  u8g2.drawStr(7, 30, "ESP CAR");
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(29, 43, "TRANSMITTER");
  u8g2.drawStr(2, 60, "developed by M.Maity");
  u8g2.sendBuffer();
  u8g2.clearBuffer();
}
////////////////////////////////////////////////////////////////////////
void setOrigin(uint8_t noOfSamples) {

  Serial.println("Setup origin(x,y)");
  clearLCD(0, 0, 128, 64);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(1, 12, "Setup origin(x,y)");
  u8g2.sendBuffer();

  unsigned long xSamplesTotal = 0;
  unsigned long ySamplesTotal = 0;

  for (uint8_t start = 1; start <= noOfSamples; start++) {
    xSamplesTotal += analogRead(JOYX);
    ySamplesTotal += analogRead(JOYY);
    clearLCD(1, 16, 128, 11);

    String count = String(start) + "/" + String(noOfSamples) + " collected";

    Serial.println(count);
    u8g2.drawStr(1, 25, count.c_str());
    u8g2.sendBuffer();
    delay(100);
  }

  originX = xSamplesTotal / noOfSamples;
  originY = ySamplesTotal / noOfSamples;
  String origin = "Origin (" + String(originX) + ", " + String(originY) + ")";

  Serial.println(origin);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(1, 41, origin.c_str());
  u8g2.sendBuffer();
  delay(4000);
}
////////////////////////////////////////////////////////////////////////
void uploadOriginValues() {

  if (!oriUploadExecuted) {

    if (firebaseStatus == "ok") {
      Firebase.setInt(firebaseData, "/ESP-CAR/X", originX);
      Firebase.setInt(firebaseData, "/ESP-CAR/Y", originY);

      Serial.println("uploaded the origin(x,y) value to server");
      u8g2.drawStr(1, 55, "uploaded to server");
      u8g2.sendBuffer();
      delay(1000);
    }
    if (firebaseStatus != "ok") {
      Serial.println("failed to upload the origin(x,y) value");
      u8g2.drawStr(1, 55, "error to upload!");
      u8g2.sendBuffer();
      delay(1000);
    }
    oriUploadExecuted = true;
  }
}
////////////////////////////////////////////////////////////////////////
void clearLCD(const long x, uint8_t y, uint8_t wid, uint8_t hig) {
  /*  this wid is right x, this height is below y
      where font wid is right x, font height is upper y
  */
  u8g2.setDrawColor(0);
  u8g2.drawBox(x, y, wid, hig);
  u8g2.setDrawColor(1);
}
/////////////////////////////////////////////////////////////////////////
void connectFirebase() {
  preferences.begin("my-app", false);

  if (preferences.getString("firebaseUrl", "") != "" && preferences.getString("firebaseToken", "") != "") {
    Serial.println("Firebase settings already exist. Checking Firebase connection...");

    clearLCD(0, 40, 128, 10);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(0, 50, "SECRETS EXIST...");
    u8g2.sendBuffer();
    delay(500);

    String firebaseUrl = preferences.getString("firebaseUrl", "");
    String firebaseToken = preferences.getString("firebaseToken", "");

    config.database_url = firebaseUrl;
    config.api_key = firebaseToken;

    Firebase.signUp(&config, &auth, "", "");  //for anonymous user

    delay(100);

    Firebase.begin(&config, &auth);

    delay(100);
    Firebase.reconnectWiFi(true);
    delay(100);

    if (isFirebaseConnected() == true) {
      Serial.println("Connected to Firebase. Skipping server setup.");
      clearLCD(0, 50, 128, 10);
      u8g2.setFont(u8g2_font_t0_11_tr);
      u8g2.drawStr(0, 60, "SERVER = OK!");
      u8g2.sendBuffer();
      delay(500);
      firebaseStatus = "ok";
    } else {
      Serial.println("Failed to connect to Firebase. Starting server setup.");
      clearLCD(0, 50, 128, 10);
      u8g2.setFont(u8g2_font_t0_11_tr);
      u8g2.drawStr(0, 60, "SERVER = ERROR!");
      u8g2.sendBuffer();
      delay(1000);
      setupServer();
    }
  } else {
    Serial.println("Firebase settings not found. Starting server setup.");
    clearLCD(0, 40, 128, 10);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(0, 50, "SECRETS NOT FOUND!");
    u8g2.sendBuffer();
    delay(500);
    setupServer();
  }
}
//////////////////////////////////////////////////////////////////
void setupServer() {
  preferences.begin("my-app", false);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/index.html", String(), false);
  });

  server.on("/Submit", HTTP_POST, [](AsyncWebServerRequest * request) {
    String firebaseUrl = request->arg("url");
    String firebaseToken = request->arg("token");

    preferences.putString("firebaseUrl", firebaseUrl);
    preferences.putString("firebaseToken", firebaseToken);

    config.database_url = firebaseUrl;
    config.api_key = firebaseToken;

    Firebase.signUp(&config, &auth, "", "");  //for anonymous user

    delay(100);

    Firebase.begin(&config, &auth);

    delay(100);
    Firebase.reconnectWiFi(true);
    delay(100);

    if (isFirebaseConnected() == true) {
      firebaseStatus = "ok";
      Serial.println("Firebase settings saved");
      Serial.println("Success");
      Serial.println("Restarting your device...");

      clearLCD(0, 40, 128, 10);
      u8g2.setFont(u8g2_font_t0_11_tr);
      u8g2.drawStr(0, 50, "SAVED. SUCCESS!");
      delay(500);
      clearLCD(0, 50, 128, 10);
      u8g2.drawStr(0, 60, "RESTARTING...");
      u8g2.sendBuffer();
      delay(1000);
      ESP.restart();
    } else {
      firebaseStatus = "";
      Serial.println("Firebase settings saved");
      Serial.println("Error! Check your credentials.");
      Serial.println("Restarting your device...");

      clearLCD(0, 40, 128, 10);
      u8g2.setFont(u8g2_font_t0_11_tr);
      u8g2.drawStr(0, 50, "SAVED. FAILED!");
      delay(500);
      clearLCD(0, 50, 128, 10);
      u8g2.drawStr(0, 60, "RESTARTING...");
      u8g2.sendBuffer();
      delay(1000);
      ESP.restart();
    }
  });

  server.serveStatic("/", SPIFFS, "/");
  server.begin();

  Serial.println("server begin");
  Serial.println(WiFi.localIP());

  clearLCD(0, 40, 128, 10);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(0, 50, "SERVER OPEN");
  clearLCD(0, 50, 128, 10);
  ipCheck(0, 60);
  u8g2.sendBuffer();
  delay(500);

  showLedStatus(0, 0, 255);


  delay(portalOpenTime);
  Serial.println("Restarting your device...");

  clearLCD(0, 50, 128, 10);
  u8g2.drawStr(0, 60, "RESTARTING...");
  u8g2.sendBuffer();
  delay(1000);

  ESP.restart();
}
//////////////////////////////////////////////////////////////////////////////
void ipCheck(uint8_t ipx, uint8_t ipy) {

  String rawIP = WiFi.localIP().toString();

  String IPAdd = "IP " + rawIP;

  clearLCD(ipx, ipy - 10, 98, 10);

  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(ipx, ipy, IPAdd.c_str());
  u8g2.sendBuffer();
}
//////////////////////////////////////////////////////////////////////////////
void connectWiFi() {

  WiFiManager wm;

  clearLCD(0, 10, 128, 30);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(0, 20, "CONNECTING WIFI...");
  u8g2.drawStr(0, 30, "AP - TRANSMITTER");
  u8g2.drawStr(0, 40, "IP - 192.168.4.1");
  u8g2.sendBuffer();

  WiFi.disconnect();
  delay(50);
  bool success = false;
  while (!success) {
    wm.setConfigPortalTimeout(60);
    success = wm.autoConnect("TRANSMITTER");
    if (!success) {
      clearLCD(0, 10, 128, 30);
      u8g2.setFont(u8g2_font_t0_11_tr);
      u8g2.drawStr(0, 20, "WIFI SETUP = ERROR!");
      u8g2.drawStr(0, 30, "AP - TRANSMITTER");
      u8g2.drawStr(0, 40, "IP - 192.168.4.1");
      u8g2.sendBuffer();

      Serial.println("TRANSMITTER");
      Serial.println("Setup IP - 192.168.4.1");
      Serial.println("Conection Failed!");
    }
  }

  Serial.print("Connected SSID - ");
  Serial.println(WiFi.SSID());
  Serial.print("IP Address is : ");
  Serial.println(WiFi.localIP());

  clearLCD(0, 10, 128, 30);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(0, 20, "WIFI SETUP = OK!");
  u8g2.sendBuffer();
  delay(1000);
  ssid = WiFi.SSID();
  u8g2.drawStr(0, 30, ssid.c_str());
  u8g2.sendBuffer();
  delay(1000);
  wifiSignalQuality(100, 30);
  delay(500);
  ipCheck(0, 40);
  delay(500);
}
////////////////////////////////////////////////////////////////////////
void wifiSignalQuality(uint8_t sqx, uint8_t sqy) {

  wifiRSSI = WiFi.RSSI() * (-1);
  char str[3];
  char str2[3] = "%";

  tostring(str, signalQuality[wifiRSSI]);

  strcat(str, str2);

  clearLCD(sqx, sqy - 9, 20, 9);

  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(sqx, sqy, str);
  u8g2.sendBuffer();
}
///////////////////////////////////////////////////////////////////////
void tostring(char str[], int num) {
  int i, rem, len = 0, n;

  n = num;
  while (n != 0) {
    len++;
    n /= 10;
  }
  for (i = 0; i < len; i++) {
    rem = num % 10;
    num = num / 10;
    str[len - (i + 1)] = rem + '0';
  }
  str[len] = '\0';
}
////////////////////////////////////////////////////////////////////////
void onDemandFirebaseConfig() {
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    u8g2.clearDisplay();
    onDemand = true;
    firebaseStatus = "";
    setupServer();
  }
  delay(100);
}

void decodeData(String data) {
  Serial.println(data);  //{"BATTERY":12.2,"BHC":12.24,"BLC":11.2,"GPS":1,"HL":1,"HORN":0,"LAT":21.86387,"LNG":88.38109,"NAVX":1370,"NAVY":1353,"SAT":0,"SPEED":0,"X":1280,"Y":1211}

  /*
      goto website https://arduinojson.org/v6/assistant/#/step1
      select board
      choose input datatype
      and paste your JSON data
      it automatically generate your code
  */
  StaticJsonDocument<385> doc;
  DeserializationError error = deserializeJson(doc, data);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }


  batteryLevel = doc["BATTERY"];
  bhc = doc["BHC"];
  blc = doc["BLC"];
  gpsValue = doc["GPS"];
  headlightValue = doc["HL"];
  hornValue = doc["HORN"];
  latti = doc["LAT"];
  longi = doc["LNG"];
  navX2 = doc["NAVX"];
  navY2 = doc["NAVY"];
  satNo = doc["SAT"];
  carSpeed = doc["SPEED"];

  linkStatus = "down";
}
////////////////////////////////////////////////////////////////
boolean isFirebaseConnected() {
  Firebase.getString(firebaseData, "/ESP-CAR");
  if (firebaseData.stringData() != "") {
    return true;
  } else {
    return false;
  }
}
//////////////////////////////////////////////////////////////
void showLedStatus(uint8_t r, uint8_t g, uint8_t b) {
  leds[STATUS_LED] = CRGB(r, g, b);
  FastLED.show();
}
///////////////////////////////////////////////////////////////
void loading() {
  static uint16_t sPseudotime = 0;
  static uint16_t sLastMillis = 0;
  static uint16_t sHue16 = 0;

  uint8_t sat8 = beatsin88(87, 220, 250);
  uint8_t brightdepth = beatsin88(341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88(203, (25 * 256), (40 * 256));
  uint8_t msmultiplier = beatsin88(147, 23, 60);

  uint16_t hue16 = sHue16;  //gHue * 256;
  uint16_t hueinc16 = beatsin88(113, 1, 3000);

  uint16_t ms = millis();
  uint16_t deltams = ms - sLastMillis;
  sLastMillis = ms;
  sPseudotime += deltams * msmultiplier;
  sHue16 += deltams * beatsin88(400, 5, 9);
  uint16_t brightnesstheta16 = sPseudotime;

  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    hue16 += hueinc16;
    uint8_t hue8 = hue16 / 256;

    brightnesstheta16 += brightnessthetainc16;
    uint16_t b16 = sin16(brightnesstheta16) + 32768;

    uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
    uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
    bri8 += (255 - brightdepth);

    CRGB newcolor = CHSV(hue8, sat8, bri8);

    uint16_t pixelnumber = i;
    pixelnumber = (NUM_LEDS - 1) - pixelnumber;

    nblend(leds[pixelnumber], newcolor, 64);
  }
}

//////////////////////////////////////////////////////////////
void gpsPowerControll() {
  if (digitalRead(GPS_POWER) == HIGH) {
    Firebase.setInt(firebaseData, "/ESP-CAR/GPS", 1);
  } else {
    Firebase.setInt(firebaseData, "/ESP-CAR/GPS", 0);
    Firebase.setInt(firebaseData, "/ESP-CAR/SAT", 0);
    Firebase.setFloat(firebaseData, "/ESP-CAR/SPEED", 00.000);
  }
}
/////////////////////////////////////////////////////////////
void navUpload() {

  if (abs(navX2 - navX) > 10 || abs(navY2 - navY) > 10) {
    Firebase.setInt(firebaseData, "/ESP-CAR/NAVX", navX);
    Firebase.setInt(firebaseData, "/ESP-CAR/NAVY", navY);
    linkStatus = "up";
  }

  if (digitalRead(HORN) == LOW) {
    Firebase.setInt(firebaseData, "/ESP-CAR/HORN", 0);
  } else {
    Firebase.setInt(firebaseData, "/ESP-CAR/HORN", 1);
  }
}
///////////////////////////////////////////////////////////////
void drawLayout() {
  u8g2.drawFrame(0, 0, 80, 64);
  u8g2.drawFrame(80, 0, 48, 64);
  u8g2.drawLine(81, 11, 126, 11);
  //  u8g2.drawEllipse(104, 37, 23, 25);
  u8g2.drawLine(103, 12, 103, 62);
  u8g2.drawLine(81, 37, 126, 37);
  u8g2.sendBuffer();
}
///////////////////////////////////////////////////////////////
void printSSID(uint8_t ssidx, uint8_t ssidy) {
  if (strlen(ssid.c_str()) > 6) {
    String shortSSID = ssid.substring(0, 7);
    String wifiName = shortSSID + "..";
    clearLCD(ssidx, ssidy - 9, 54, 9);

    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(ssidx, ssidy, wifiName.c_str());
    u8g2.sendBuffer();
  } else {
    String wifiName = ssid;
    clearLCD(ssidx, ssidy - 9, 54, 9);

    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(ssidx, ssidy, wifiName.c_str());
    u8g2.sendBuffer();
  }
}
///////////////////////////////////////////////////////////////
void batteryVoltage(uint8_t bvx, uint8_t bvy) {
  String level = String(batteryLevel, 1);
  String inUnit = "B=" + level + "V";
  clearLCD(bvx, bvy - 9, 54, 9);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(bvx, bvy, inUnit.c_str());
  u8g2.sendBuffer();
}
///////////////////////////////////////////////////////////////
void batteryPercent(uint8_t bpx, uint8_t bpy) {
  if (batteryLevel >= blc) {
    float batteryFactor = 99 / (bhc - blc);
    int bat = (batteryLevel - blc) * batteryFactor;
    String percentStr = String(bat);
    String percent = percentStr + "%";
    clearLCD(bpx, bpy - 9, 20, 9);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(bpx, bpy, percent.c_str());
    u8g2.sendBuffer();
  } else {
    String percent = "00%";
    clearLCD(bpx, bpy - 9, 20, 9);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(bpx, bpy, percent.c_str());
    u8g2.sendBuffer();
  }
}
///////////////////////////////////////////////////////////////
void displayHorn(uint8_t dhx, uint8_t dhy) {
  if (hornValue == 1) {
    clearLCD(dhx, dhy - 9, 12, 9);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(dhx, dhy, "HR");
    u8g2.sendBuffer();
  } else {
    clearLCD(dhx, dhy - 9, 12, 9);
  }
}
///////////////////////////////////////////////////////////////
void displayHeadlight(uint8_t dhdx, uint8_t dhdy) {
  if (headlightValue == 1) {
    clearLCD(dhdx, dhdy - 9, 12, 9);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(dhdx, dhdy, "HL");
    u8g2.sendBuffer();
  } else {
    clearLCD(dhdx, dhdy - 9, 12, 9);
  }
}
///////////////////////////////////////////////////////////////
void displayNav() {
  navX = analogRead(JOYX);
  navY = analogRead(JOYY);

  Serial.print(" X: ");
  Serial.print(navX);
  Serial.print(" Y: ");
  Serial.println(navY);

  int xR = map(navX, originX, 4095, 100, 121);
  int xL = map(navX, originX, 0, 100, 80);
  int yT = map(navY, originY, 0, 41, 18);
  int yB = map(navY, originY, 4095, 41, 64);


  clearLCD(104, 12, 23, 25);
  clearLCD(81, 12, 22, 25);
  clearLCD(81, 38, 22, 25);
  clearLCD(104, 38, 23, 25);

  u8g2.setFont(u8g2_font_t0_11_tr);

  if (navX > originX && navY < originY) {  //1st co.
    u8g2.drawStr(xR, yT, "+");
  }
  if (navX < originX && navY < originY) {  //2nd co.
    u8g2.drawStr(xL, yT, "+");
  }
  if (navX < originX && navY > originY) {  //3rd co.
    u8g2.drawStr(xL, yB, "+");
  }
  if (navX > originX && navY > originY) {  //4th co.
    u8g2.drawStr(xR, yB, "+");
  }
  u8g2.sendBuffer();
}
///////////////////////////////////////////////////////////////
void displayGPSStatus(uint8_t dgx, uint8_t dgy) {
  if (gpsValue == 1) {
    clearLCD(dgx, dgy - 9, 50, 9);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(dgx, dgy, "GPS= ON");
    u8g2.sendBuffer();

    String satStr = String(satNo);
    clearLCD(dgx + 53, dgy - 9, 20, 9);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(dgx + 53, dgy, satStr.c_str());
    u8g2.sendBuffer();

  } else {
    clearLCD(dgx, dgy - 9, 77, 9);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(dgx, dgy, "GPS= OFF");
    u8g2.sendBuffer();
  }
}
///////////////////////////////////////////////////////////////
void displayLatLng(uint8_t dllx, uint8_t dlly) {
  concatenatedDMSLat = "";
  concatenatedDMSLng = "";

  convertToDMS(latti, 'N', 'S', concatenatedDMSLat);
  convertToDMS(longi, 'E', 'W', concatenatedDMSLng);

  clearLCD(dllx, dlly - 9, 77, 9);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(dllx, dlly, concatenatedDMSLat.c_str());

  clearLCD(dllx, dlly + 10 - 9, 77, 9);
  u8g2.drawStr(dllx, dlly + 10, concatenatedDMSLng.c_str());
  u8g2.sendBuffer();
}
//////////////////////////////////////////////////////////////
void convertToDMS(double value, char positiveDirection, char negativeDirection, String &resultString) {
  char direction = (value >= 0) ? positiveDirection : negativeDirection;
  value = fabs(value);
  int degrees = static_cast<int>(value);
  double minutes = (value - degrees) * 60.0;
  double seconds = (minutes - static_cast<int>(minutes)) * 60.0;
  resultString += String(degrees) + "." + String(static_cast<int>(minutes)) + "'" + String(seconds, 0) + "\"" + direction;
}
///////////////////////////////////////////////////////////////
void displayCarSpeed(uint8_t dcsx, uint8_t dcsy) {

  String speedStr = String(carSpeed, 1);
  String speedStr2 = "MPS= " + speedStr;

  clearLCD(dcsx, dcsy - 9, 77, 9);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(dcsx, dcsy, speedStr2.c_str());
  u8g2.sendBuffer();
}
///////////////////////////////////////////////////////////////
void upDownlink(String linkStatus) {
  if (linkStatus == "up") {
    clearLCD(98, 3, 11, 7);
    u8g2.drawXBM(98, 3, 5, 7, image_arrow_up_bits);
    //    showLedStatus(0, 255, 255);
  }
  if (linkStatus == "down") {
    clearLCD(98, 3, 11, 7);
    u8g2.drawXBM(104, 3, 5, 7, image_arrow_down_bits);
    //    showLedStatus(255, 255, 0);
  }
}
///////////////////////////////////////////////////////////////
void loop1(void *parameter) {

  for (;;) {
    drawLayout();
    if (WiFi.status() == WL_CONNECTED && firebaseStatus == "ok") {
      showLedStatus(0, 255, 0);
      upDownlink(linkStatus);
      printSSID(2, 10);
      wifiSignalQuality(55, 10);
      batteryVoltage(2, 20);
      batteryPercent(55, 20);
      displayHorn(81, 10);
      displayHeadlight(115, 10);
      displayNav();
      displayGPSStatus(2, 30);
      displayLatLng(2, 40);
      displayCarSpeed(2, 60);
    }
    if (onDemand == true) {
      loading();
      FastLED.show();
    }
    if (WiFi.status() != WL_CONNECTED) {
      showLedStatus(255, 0, 0);
      connectWiFi();
    }
  }
}

//////////////////////////////////////////////////////////////

void loop() {

  onDemand = false;
  onDemandFirebaseConfig();

  if (firebaseStatus == "ok") {
    navUpload();
    gpsPowerControll();
    Firebase.getString(firebaseData, "/ESP-CAR");
    decodeData(firebaseData.stringData());
  } else {
    Serial.println("firebase failed");
  }

  if (firebaseStatus != "ok") {
    if (WiFi.status() == WL_CONNECTED) {
      Firebase.getString(firebaseData, "/ESP-CAR");
      decodeData(firebaseData.stringData());
    }
  }
}
