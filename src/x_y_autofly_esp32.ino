// Define Serial print functions as debug functions
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)

// Command parsing 
char buffer[512];
char character; 

// Built-In OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Stepper variables
#include <ESP_FlexyStepper.h>
#define LIMIT_SWITCH_PIN_X 5
#define LIMIT_SWITCH_PIN_Y 25
//198720 is the number of steps to travel a full length at 32 microsteps
//(198720/1 = 32) (198720/2 = 16) (/4 = 8) (/8 = 4) (/16 = 2) (/32 = 1)
#define stepDivisor 2
#define fullLength -198720/stepDivisor
#define segmentLen fullLength/9
#define stepPinX 14
#define dirPinX 12
#define stepPinY 13
#define dirPinY 15
const int SPEED_IN_STEPS_PER_SECOND = 4000;
const int ACCELERATION_IN_STEPS_PER_SECOND = 4000;
const int DECELERATION_IN_STEPS_PER_SECOND = 4000;
int xTarget = 0;
int yTarget = 0;
ESP_FlexyStepper stepperX, stepperY;
//Flag that branches the task loop into the main program task when true
//In the main program task a for loop moves the steppers to the next
//  Cell that is requesting to be filled
bool runningMain = false;

// Pump
#define pumpRelay 16
int duration = 1000;
float pumpRate = 1;//pumping rate in mL/s
bool pumping = false;

// OTA & WiFi & Async webserver

#include <WiFi.h>
#include <ESPAsync_WiFiManager.h> 
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include "ESPAsyncWebServer.h"
AsyncWebServer server(80);
DNSServer dnsServer;
String ssid = "";
String pass = "";
String apssid = "XYFLYAP";

//ESP-NOW variables
#include "ESPNowW.h"
typedef struct struct_message 
{
    int ID;
    int isVial;
    int isBottle;
    int isFilled;
}struct_message;
struct_message message;
uint8_t camMACAddress[] = {0x30, 0xC6, 0xF7, 0x04, 0xA8, 0xCC};

// Initialize all the periphreals and BT
void setup(){initialize_XYFLY();}

//constantly idling
void loop(){idle();}

void idle()
{
  ArduinoOTA.handle();
  dnsServer.processNextRequest();
  if(runningMain){fill_all_empty_vials();}
  delay(10);
}

void update_status(String updated_status)
{
  debugln();
  debug("STATUS UPDATE: ");
  debugln(updated_status);
  debugln();
  display.clearDisplay();
  delay(100);
  display.setCursor(0, 0);
  display.println(updated_status);
  display.display();
}

void go_home()
{
  update_status("Going home...");
  move_to_X_Y(0, 0);
}

void set_home()
{       
  update_status("Setting home...");
  stepperX.setCurrentPositionAsHomeAndStop();
  stepperY.setCurrentPositionAsHomeAndStop();
}

void calibrate_pump()
{
  digitalWrite(pumpRelay, HIGH);
  delay(1000);
  digitalWrite(pumpRelay, LOW);
}

void toggle_pump()
{
  if(pumping)
  {
    pumping = false; 
    update_status("Turning pump off.");
    digitalWrite(pumpRelay, LOW);
  }
  else
  {
    pumping = true; 
    update_status("Turning pump on.");
    digitalWrite(pumpRelay, HIGH);
  }
}

void pump(int cell_duration = 0)
{
  sprintf(buffer, "Dispensing for: %i ms", duration);
  update_status(buffer);
  digitalWrite(pumpRelay, HIGH);
  if(cell_duration > 0){delay(cell_duration);}else{delay(duration);}
  digitalWrite(pumpRelay, LOW);
}

void fill_all_empty_vials()
{
  for (int x = 0; x < 10 ; x++)
  {
    for(int y = 0; y < 10 ; y++)
    {
      update_status("Revise this fn");
    }
  }
}

void move_to_X_Y(float X_, float Y_)
{
  xTarget = round(X_);
  yTarget = round(Y_);
  stepperX.setTargetPositionInSteps(xTarget);
  stepperY.setTargetPositionInSteps(yTarget);
  sprintf(buffer, "Set stepper target step position [X:%i, Y:%i]", xTarget, yTarget);
  debugln(buffer);
}

void move_to_prev_X()
{
  float X_ = constrain(xTarget-segmentLen, fullLength, 0);
  move_to_X_Y(X_, yTarget);
}

void move_to_next_X()
{
  float X_ = constrain(xTarget+segmentLen, fullLength, 0);
  move_to_X_Y(X_, yTarget);
}

void move_to_prev_Y()
{
  float Y_ = constrain(yTarget-segmentLen, fullLength, 0);
  move_to_X_Y(xTarget, Y_);
}

void move_to_next_Y()
{
  float Y_ = constrain(yTarget+segmentLen, fullLength, 0);
  move_to_X_Y(xTarget, Y_);
}

bool save_config() 
{
  update_status("Saving config...");
  update_status("Mount filesystem");
  if (SPIFFS.begin()) 
  {
    update_status("Filesystem mounted");
    if(SPIFFS.exists("/config.json"))
    {
      update_status("Old config found");
      update_status("Deleting config");
      SPIFFS.remove("/config.json");
      update_status("Creating config");
      SPIFFS.mkdir("/config.json");
    }else
    {
      update_status("No config found");
      update_status("Creating config");
      SPIFFS.mkdir("/config.json");
    }
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile){debugln("Config open fail");return false;}
    update_status("Config open");
    StaticJsonDocument<1024> doc;
    // Set the values in the document
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    update_status("Config set");
    // Serialize JSON to file
    if (serializeJson(doc, configFile) == 0) {update_status("Config write fail");}else{update_status("Config written");}
    configFile.flush();
    configFile.close();
    update_status("Config closed");
    return true;
  }else
  {
    update_status("Filesystem mount fail");
    return false;
  }
}

bool read_config()
{
  update_status("Reading config...");
  //read configuration from FS json
  update_status("Mount filesystem");
  if (SPIFFS.begin()) 
  {
    update_status("Filesystem mounted");
    if (SPIFFS.exists("/config.json")) 
    {
      //file exists, reading and loading
      update_status("Config found");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) 
      {
        update_status("Opened config");
        StaticJsonDocument<1024> doc;
        update_status("Reading config");
        // Deserialize the JSON document
        DeserializationError error = deserializeJson(doc, configFile);
        // Test if parsing succeeds.
        if (error) 
        {
          update_status(error.f_str());
          update_status("JSON parse fail");
          delay(1000);
          return false;
        }
        debug("SSID before config read: ");
        debugln(ssid);
        debug("Pass before config read: ");
        debugln(pass);
        ssid = doc["ssid"].as<String>();
        pass = doc["pass"].as<String>();
        debug("SSID from config: ");
        debugln(ssid);
        debug("Pass from config: ");
        debugln(pass);
        configFile.close();
        update_status("Config closed");
      }
    }else{update_status("No config file");save_config();return false;}
  } else {update_status("Filesystem error");return false;}
  // read successful
  update_status("Config read done");
  return true;
}

void handle_input(String input)
{
    sprintf(buffer, "Input: %s", input.c_str());
    update_status(buffer);
    if(input == "reset"){update_status("HARD RESET");delay(1000);ESP.restart();}
    if(input == "stop"){runningMain = false;stepperX.emergencyStop();stepperY.emergencyStop();}
    if(input == "stopX"){stepperX.emergencyStop();}
    if(input == "stopY"){stepperY.emergencyStop();}
    if(input == "gohome"){go_home();}
    if(input == "sethome"){set_home();}
    if(input == "runmain"){runningMain = true;}
    if(input == "stopmain"){runningMain = false;}
    if(input == "dispense"){toggle_pump();}
    if(input == "right"){stepperX.startJogging(1);}
    if(input == "left"){stepperX.startJogging(-1);}
    if(input == "up"){stepperY.startJogging(1);}
    if(input == "down"){stepperY.startJogging(-1);}
    if(input == "pump1s"){calibrate_pump();}
    if(input.indexOf("dur") == 0){duration = int((input.substring(input.indexOf(".")+1, input.length()).toFloat())/pumpRate);}
    if(input.indexOf("cal") == 0){pumpRate = float(input.substring(input.indexOf(".")+1, input.length()).toFloat())/1000;}
    if(input.indexOf("goX") == 0)
    {
      float goX = input.substring(3, input.indexOf("goY")).toFloat();
      goX = goX/100;
      goX = goX*fullLength;
      float goY = input.substring(input.indexOf("goY")+3, input.length()).toFloat();
      goY = goY/100;
      goY = goY*fullLength;
      move_to_X_Y(goX, goY);
    }
    if(input.indexOf("ssid.") == 0)
    {
      String ssid_ = input.substring(input.indexOf("ssid.")+5, input.indexOf(".pass."));
      String pass_ = input.substring(input.indexOf(".pass.")+6, input.length());
      ssid = ssid_;
      pass = pass_;
      initialize_wifi();
    }
}

// Can replace placeholder with variable from the esp32
String processor(const String& var)
{
  // if (var == "current_ssid_status")
  // {
  //   return ssid;
  // }
  return String();
}

//Initialize function that starts serial connections sets pin modes etc...
void initialize_XYFLY()
{
  // Start and init serial connection
  Serial.begin(115200);
  Serial.setTimeout(10000);
  // Begin OLED communication (always first after serial init to avoid errors when updating status)
  initialize_OLED(); 
  // Start WiFi
  initialize_wifi();
  // Start OTA service
  initialize_OTA();
  // Setup async web server
  initialize_webserver();
  // Stepper setup
  initialize_steppers();
  // Update user
  if(WiFi.isConnected())
    {
      sprintf(buffer, "MAC:%s",  WiFi.macAddress().c_str());
      update_status(buffer);
      sprintf(buffer, "IP:%s",  WiFi.localIP().toString().c_str());
      update_status(buffer);
    }else
    {
      sprintf(buffer, "Wifi:%s IP:%s", apssid.c_str(), WiFi.softAPIP().toString().c_str());
      update_status(buffer);
    }
}

void initialize_steppers()
{
  // Set pump pin as output
  pinMode(pumpRelay, OUTPUT);
  stepperX.setDirectionToHome(1);
  stepperX.connectToPins(stepPinX, dirPinX);
  stepperX.setSpeedInStepsPerSecond(SPEED_IN_STEPS_PER_SECOND);
  stepperX.setAccelerationInStepsPerSecondPerSecond(ACCELERATION_IN_STEPS_PER_SECOND);
  stepperX.setDecelerationInStepsPerSecondPerSecond(DECELERATION_IN_STEPS_PER_SECOND);
  stepperX.startAsService();
  stepperY.setDirectionToHome(1);
  update_status("X stepper service started");
  stepperY.connectToPins(stepPinY, dirPinY);
  stepperY.setSpeedInStepsPerSecond(SPEED_IN_STEPS_PER_SECOND);
  stepperY.setAccelerationInStepsPerSecondPerSecond(ACCELERATION_IN_STEPS_PER_SECOND);
  stepperY.setDecelerationInStepsPerSecondPerSecond(DECELERATION_IN_STEPS_PER_SECOND);
  stepperY.startAsService();
  update_status("Y stepper service started");
}

void initialize_OLED()
{
  Wire.begin(5, 4);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {debugln(F("SSD1306 allocation failed"));for(;;);}else
  {
    // Clear the buffer.  
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(4);
    display.setTextColor(WHITE);
    update_status("OLED up");
    delay(1000);
    display.clearDisplay();
    display.setCursor(0, 0);
    update_status("Welcome");
    delay(1000);
    display.clearDisplay();
    display.setCursor(0, 0);
    update_status("XYFLY BETA");
    delay(1000);
    display.setTextSize(2);
    display.setTextColor(WHITE);
  }
}

void initialize_OTA()
{
  update_status("Booting OTA");
  ArduinoOTA
    .onStart([]() 
    {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      update_status("OTA update of type: " + type);
      debugln("OTA update of type: " + type);
    })
    .onEnd([]() 
    {
      debugln("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) 
    {
      sprintf(buffer, "Progress: %u%%\r", (progress / (total / 100)));
      update_status(buffer);
      Serial.printf(buffer);
    })
    .onError([](ota_error_t error) 
    {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) debugln("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) debugln("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) debugln("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) debugln("Receive Failed");
      else if (error == OTA_END_ERROR) debugln("End Failed");
    });
  ArduinoOTA.begin();
  update_status("OTA booted");
}

void initialize_wifi()
{
  if(WiFi.isConnected()){update_status("Wifi already connected! Updating config.");save_config();return;}
  if(ssid != ""){save_config();}
  read_config();
  WiFi.mode(WIFI_STA);
  ESPNow.init();
  ESPNow.reg_recv_cb(data_receive);
  ESPNow.add_peer(camMACAddress, 0);
  WiFi.begin(ssid.c_str(), pass.c_str());
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    update_status("Default fail! Starting AP.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apssid.c_str());
    server.begin();
    update_status("No Wifi - AP started");
    return;
  }
  update_status("Wifi router connected");
}

void initialize_webserver()
{
  if(WiFi.getMode() == WIFI_MODE_STA){WiFi.softAP(apssid.c_str());}
  dnsServer.start(53, "*", WiFi.softAPIP());
  update_status("DNS server started");
  //On get requests do things
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(SPIFFS, "/config.html", String(), false, processor);
  });
  server.on("/command", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    String inputValue = request->getParam("state")->value();
    handle_input(inputValue);
    request->send(200, "text/plain", "OK");
  });
  server.begin();
  update_status("Webserver up");
}

void data_receive(const uint8_t * mac_addr, const uint8_t *incomingData, int len) 
{
  memcpy(&message, incomingData, sizeof(message));
  sprintf(buffer, "Received NOW data from Board: %u\n Size: %u bytes\n isVial: %u\n isBottle: %u\n isFilled: %u\n", message.ID, len, message.isVial, message.isBottle, message.isFilled);
  debugln(buffer);
  delay(1);
  if(message.isVial && !message.isFilled)
  {
    pump();
  }
}
