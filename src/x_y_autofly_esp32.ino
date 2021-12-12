// Define Serial print functions as debug functions
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)

// Command parsing 
char character; 
String command;
char buffer[40];

// Built-In OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Stepper variables
#include <ESP_FlexyStepper.h>
#define LIMIT_SWITCH_PIN 25
#define fullLength -198720/2 //length 198720 is in number of steps at 32 so that maximized resolution 198720/2 at 16 198720/4 at 8 /8 at 4 /16 at 2 /32 at 1 etc
#define segment_len fullLength/9
#define dirPin_X 12
#define stepPin_X 14
#define dirPin_Y 15
#define stepPin_Y 13
const int SPEED_IN_STEPS_PER_SECOND = 2000;
const int ACCELERATION_IN_STEPS_PER_SECOND = 2000;
const int DECELERATION_IN_STEPS_PER_SECOND = 2000;
int X_target = 0;
int Y_target = 0;
ESP_FlexyStepper stepper_X, stepper_Y;

// Pump
#define pumpRelay 16
int duration = 1000;
float pump_rate = 1;//pumping rate in mL/s
bool pumping = false;

// OTA & WiFi * Async web server
#include <WiFi.h>
#include <ESPAsync_WiFiManager.h>              //https://github.com/khoih-prog/ESPAsync_WiFiManager
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


// Initialize all the periphreals and BT
void setup(){initialize_XYFLY();}

//constantly idling
void loop(){idle();}

void idle()
{
  ArduinoOTA.handle();
  dnsServer.processNextRequest();
  stepper_X.processMovement();
  stepper_Y.processMovement();
  delay(10);
}

void update_status(String updated_status)
{
  debugln();
  debug("STATUS UPDATE: ");
  debugln(updated_status);
  debugln();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(updated_status);
  display.display();
}

void go_home()
{
  update_status("Running home...");
  stepper_X.goToLimitAndSetAsHome();
  stepper_Y.goToLimitAndSetAsHome();
  X_target = 0;
  Y_target = 0;
}

void set_home()
{       
  update_status("Setting home...");
  stepper_X.setCurrentPositionAsHomeAndStop();
  stepper_Y.setCurrentPositionAsHomeAndStop();
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

void pump()
{//time on is in ms
  wait_for_steppers();
  sprintf(buffer, "Dispensing for: %i ms", duration);
  update_status(buffer);
  digitalWrite(pumpRelay, HIGH);
  delay(duration);
  digitalWrite(pumpRelay, LOW);
  delay(500);
}

void move_to_X_Y(float X_, float Y_)
{
  stepper_X.setTargetPositionInSteps(X_);
  stepper_Y.setTargetPositionInSteps(Y_);
  X_target = round(X_);
  Y_target = round(Y_);
  sprintf(buffer, "Moving to X:%i Y:%i", X_target, Y_target);
  update_status(buffer);
}

void move_to_prev_X()
{
  float X_ = constrain(X_target-segment_len, fullLength, 0);
  move_to_X_Y(X_, Y_target);
}

void move_to_next_X()
{
  float X_ = constrain(X_target+segment_len, fullLength, 0);
  move_to_X_Y(X_, Y_target);
}

void move_to_prev_Y()
{
  float Y_ = constrain(Y_target-segment_len, fullLength, 0);
  move_to_X_Y(X_target, Y_);
}

void move_to_next_Y()
{
  float Y_ = constrain(Y_target+segment_len, fullLength, 0);
  move_to_X_Y(X_target, Y_);
}

void run_to_all_Y_positions(bool forward)
{
  if(forward)
  {//dispense in forward direction
    for (int i = 0; i <= 9; i++)
    {
      pump();
      sprintf(buffer, "Run to Y%d", i);
      update_status(buffer);
      move_to_next_Y();
      wait_for_steppers();
    }
  }else
  {//dispense in reverse direction
    for (int i = 9; i >= 0; i--)
    {
      pump();
      sprintf(buffer, "Run to Y%d", i);
      update_status(buffer);
      move_to_prev_Y();
      wait_for_steppers();
    }
  }
}

void run_to_all_positions(int num_runs = 1)
{
  go_home();
  bool flip_flop_x = false;
  bool flip_flop_y = false;
  for(int x = 0; x < num_runs; x++)
  {
    flip_flop_x = !flip_flop_x;
    if(flip_flop_x)
    {
      for (int i = 0; i < 9; i++)
      {
        flip_flop_y = !flip_flop_y;
        sprintf(buffer, "Run to X%d", i);
        update_status(buffer);
        run_to_all_Y_positions(flip_flop_y);
        move_to_next_X();
        wait_for_steppers();
      }
    }else
    {    
      for (int i = 9; i < 0; i--)
      {
        flip_flop_y = !flip_flop_y;
        sprintf(buffer, "Run to X%d", i);
        update_status(buffer);
        run_to_all_Y_positions(flip_flop_y);
        move_to_next_X();
        wait_for_steppers();
      }
    }
  }
  go_home();
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

void wait_for_steppers(){while(!stepper_X.motionComplete() or !stepper_Y.motionComplete()){debug("Waiting for steppers.");delay(1);vTaskDelay(1);}}

void handle_input(String input)
{
    if(input == "gohome"){go_home();}
    if(input == "sethome"){set_home();}
    if(input == "runmain"){run_to_all_positions();}
    if(input == "dispense"){toggle_pump();}
    if(input == "nextX"){move_to_next_X();}
    if(input == "prevX"){move_to_prev_X();}
    if(input == "nextY"){move_to_next_Y();}
    if(input == "prevY"){move_to_prev_Y();}
    if(input == "pump1s"){calibrate_pump();}
    if(input.indexOf("dur") == 0){duration = int((input.substring(input.indexOf(".")+1, input.length()).toFloat())/pump_rate);}
    if(input.indexOf("cal") == 0){pump_rate = float(input.substring(input.indexOf(".")+1, input.length()).toFloat())/1000;}
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
      debugln("Recevied ssid input");
      String ssid_ = input.substring(input.indexOf("ssid.")+5, input.indexOf(".pass."));
      String pass_ = input.substring(input.indexOf(".pass.")+6, input.length());
      debugln("SSID:" + ssid_);
      debugln("Pass:" + pass_);
      ssid = ssid_;
      pass = pass_;
      initialize_WiFi();
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
  // Set pump pin as output
  pinMode(pumpRelay, OUTPUT);
  // Start WiFi
  initialize_WiFi();
  // Start OTA service
  initialize_OTA();
  // Setup async web server
  initialize_Webserver();
  // Stepper setup
  initialize_Steppers();
  // Update user
  if(WiFi.isConnected()){update_status("Connect to" + WiFi.localIP().toString());}
}

void initialize_Steppers()
{
  stepper_X.connectToPins(stepPin_X, dirPin_X);
  stepper_X.setSpeedInStepsPerSecond(SPEED_IN_STEPS_PER_SECOND);
  stepper_X.setAccelerationInStepsPerSecondPerSecond(ACCELERATION_IN_STEPS_PER_SECOND);
  stepper_X.setDecelerationInStepsPerSecondPerSecond(DECELERATION_IN_STEPS_PER_SECOND);
  stepper_X.setDirectionToHome(-1);
  stepper_X.startAsService();
  update_status("X stepper service started");
  stepper_Y.connectToPins(stepPin_Y, dirPin_Y);
  stepper_Y.setSpeedInStepsPerSecond(SPEED_IN_STEPS_PER_SECOND);
  stepper_Y.setAccelerationInStepsPerSecondPerSecond(ACCELERATION_IN_STEPS_PER_SECOND);
  stepper_Y.setDecelerationInStepsPerSecondPerSecond(DECELERATION_IN_STEPS_PER_SECOND);
  stepper_Y.setDirectionToHome(-1);
  stepper_Y.startAsService();
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

void initialize_WiFi()
{
  if(WiFi.isConnected()){update_status("Wifi already connected!");save_config();return;}
  if(ssid != "" && pass != ""){save_config();}
  read_config();
  WiFi.mode(WIFI_AP);
  WiFi.begin(ssid.c_str(), pass.c_str());
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    update_status("Connection fail.");
    update_status("Retry with hardcode.");
    WiFi.begin("grpRNAi", "puckered1234");
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
      update_status("Default fail! Resetting.");
      update_status("1...");
      delay(1000);
      update_status("2...");
      delay(1000);
      update_status("3...");
      delay(1000);
      ESP.restart();
    }
  }
  update_status("WiFi connected");
}

void initialize_Webserver()
{
  WiFi.softAP("XY-captive");
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
  server.on("/set_config", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    String inputValue = request->getParam("state")->value();
    debugln("/set_config" + inputValue);
    handle_input(inputValue);
    request->send(200, "text/plain", "OK");
  });
  server.on("/run_main", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    String inputValue = request->getParam("state")->value();
    debugln();
    debug("/run_main?state=");
    debugln(inputValue);
    handle_input(inputValue);
    request->send(200, "text/plain", "OK");
  });
  server.on("/nextX", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    String inputValue = request->getParam("state")->value();
    debugln();
    debug("/nextX?state=");
    debugln(inputValue);
    handle_input(inputValue);
    request->send(200, "text/plain", "OK");
  });
  server.begin();
  update_status("Webserver up");
}
