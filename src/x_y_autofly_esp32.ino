// Define Serial print functions as debug functions
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)

// Command parsing 
char buffer[512];
char character; 

// Built-In OLED
bool useScreen = false;
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
//To calculate the stepDivisor take the microStep and divide by the max microStep (32) for example:
//    (32/microStep = stepDivisor)
//    (32/2 = 16) (32/4 = 8) (32/8 = 4) (32/16 = 2) (32/32 = 1)
int microStep = 16;
int stepDivisor = 32/microStep;
int fullLength = 198720/stepDivisor;
int numSubdivisions = 10;
int segmentLen = fullLength/(numSubdivisions-1);
#define stepPinX 14
#define dirPinX 12
#define stepPinY 13
#define dirPinY 15
int SPEED_IN_STEPS_PER_SECOND = 4000;
const int ACCELERATION_IN_STEPS_PER_SECOND = 4000;
const int DECELERATION_IN_STEPS_PER_SECOND = 4000;
int xTarget = 0;
int yTarget = 0;
ESP_FlexyStepper stepperX, stepperY;

// Pump
#define pumpRelay 16
int duration = 1000;
float pumpRate = 1;//pumping rate in mL/s
bool pumping = false;
bool fillAll = false;

// OTA & WiFi & Async webserver
#include <WiFi.h>
#include <ESPAsync_WiFiManager.h> 
#include <ArduinoJson.h>
#include <HTTPClient.h>
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

// Timer functions to keep up with various asynchronous tasks
int timer0 = 0;

// Initialize all the periphreals and BT
void setup(){initialize_XYFLY();}

//constantly idling
void loop(){idle();}

void idle()
{
  ArduinoOTA.handle();
  dnsServer.processNextRequest();
  delay(1);
  //if the screen is in use then incremement a timer to clear the screen
  if(useScreen)
  {
    //increment the timer to keep up with clearing the screen occasionally
    timer0++;
    if(timer0 >= 1000)
    {
      timer0 = 0;
      display.clearDisplay();
    }
  }
  // If the main function is being run then fill all vials
  if(fillAll){fillAllVials();}
}

//Function that takes in a String and then outputs that String to all displays in this project
void update_status(String updated_status)
{
  debugln("STATUS UPDATE: ");
  debug(updated_status);
  debugln();
  // If the screen is in use then update the display
  if(useScreen)
  {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(updated_status);
    display.display();
  }
}

//Tell the steppers to move to each position and remove each position that has been filled from the list
void fillAllVials()
{
  update_status("Function 'fillAllVials()' not yet defined");
}

//Tell both steppers to go to the Home position or x = 0 and y = 0
void go_home()
{
  update_status("Going home...");
  move_to_X_Y(0, 0);
}

//Set the current position as the Home position (x = 0, y = 0)
void set_home()
{       
  update_status("Setting home...");
  stepperX.setCurrentPositionAsHomeAndStop();
  stepperY.setCurrentPositionAsHomeAndStop();
}

//Use a specified amount of ON time to calibrate the amount of fluid dispensed in that time period
void calibrate_pump()
{
  digitalWrite(pumpRelay, HIGH);
  delay(1000);
  digitalWrite(pumpRelay, LOW);
}

//Turn the pump on if it off and off if it is on
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

//Turn the pump on for a specified amount of time or default to the default amount of time
void pump(int _duration = 0)
{
  //if no custom duration is sent then set custom duration to default duration
  if(_duration <= 0){_duration = duration;}
  sprintf(buffer, "Dispensing for: %i ms", _duration);
  update_status(buffer);
  digitalWrite(pumpRelay, HIGH);
  delay(_duration);
  digitalWrite(pumpRelay, LOW);
}
void handleWifi(String input)
{
  String ssid_ = input.substring(input.indexOf("ssid.")+5, input.indexOf(".pass."));
  String pass_ = input.substring(input.indexOf(".pass.")+6, input.length());
  ssid = ssid_;
  pass = pass_;
  save_config();
  if(!WiFi.isConnected()){initialize_wifi();}
}
void parseXY(String input)
{
  float goX = input.substring(3, input.indexOf("goY")).toFloat();
  goX = goX/100;
  goX = goX*fullLength;
  float goY = input.substring(input.indexOf("goY")+3, input.length()).toFloat();
  goY = goY/100;
  goY = goY*fullLength;
  move_to_X_Y(goX, goY);
}
//Move steppers to a specific point within the limits of the x and y axes
void move_to_X_Y(float _x, float _y)
{
  xTarget = round(_x);
  yTarget = -round(_y);
  stepperX.setTargetPositionInSteps(xTarget);
  stepperY.setTargetPositionInSteps(yTarget);
  sprintf(buffer, "Set stepper target step position [X:%i, Y:%i]", xTarget, yTarget);
  debugln(buffer);
}

//Move to the previous x position
void move_to_prev_X()
{
  float X_ = constrain(xTarget-segmentLen, fullLength, 0);
  move_to_X_Y(X_, yTarget);
}

//Move to the next x position
void move_to_next_X()
{
  float X_ = constrain(xTarget+segmentLen, fullLength, 0);
  move_to_X_Y(X_, yTarget);
}

//Move to the previous y position
void move_to_prev_Y()
{
  float Y_ = constrain(yTarget-segmentLen, fullLength, 0);
  move_to_X_Y(xTarget, Y_);
}

//Move to the next y position
void move_to_next_Y()
{
  float Y_ = constrain(yTarget+segmentLen, fullLength, 0);
  move_to_X_Y(xTarget, Y_);
}

void parseMicrostep(String input)
{
  microStep = input.substring(input.indexOf("microstep")+9, input.length()).toInt();
  stepDivisor = 32/microStep;
  fullLength = 198720/stepDivisor;
  segmentLen = fullLength/(numSubdivisions-1);
}

void updateSpeed(String input)
{
  SPEED_IN_STEPS_PER_SECOND = input.substring(input.indexOf("speed")+5, input.length()).toInt();
  stepperX.setSpeedInStepsPerSecond(SPEED_IN_STEPS_PER_SECOND);
  stepperY.setSpeedInStepsPerSecond(SPEED_IN_STEPS_PER_SECOND);
}

//Save the current WiFi config values in the local SPIFFS system
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

//Read the config from the SPIFFS filesystem
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

//Switch on a String value to control logic flow through the web based UI
void handle_input(String input)
{
    sprintf(buffer, "Input: %s", input.c_str());
    update_status(buffer);
    if(input == "reset"){update_status("HARD RESET");delay(1000);ESP.restart();}//hard reset
    if(input == "stop"){fillAll = false;stepperX.emergencyStop();stepperY.emergencyStop();}//emergency stop all operations
    if(input == "stopX"){stepperX.emergencyStop();}//emergency stop for x stepper
    if(input == "stopY"){stepperY.emergencyStop();}//emergency stop for y stepper
    if(input == "gohome"){go_home();}//go to 0,0
    if(input == "sethome"){set_home();}//set the current position as 0,0
    if(input == "fillall"){fillAll = true;}//enable async fill operations
    if(input == "stopfillall"){fillAll = false;}//halt the async fill operations
    if(input == "dispense"){toggle_pump();}//turn the pump on/off
    if(input == "right"){stepperX.startJogging(1);}//move x stepper in positive direction
    if(input == "left"){stepperX.startJogging(-1);}//move x stepper in negative direction
    if(input == "up"){stepperY.startJogging(1);}//move y stepper in positive direction
    if(input == "down"){stepperY.startJogging(-1);}//move y stepper in negative direction
    if(input == "pump1s"){calibrate_pump();}//activate the pump for one second
    if(input == "fullvial"){}//capture a full vial reference image
    if(input == "emptyvial"){}//capture an empty vial reference image
    if(input.indexOf("dur") == 0){duration = int((input.substring(input.indexOf(".")+1, input.length()).toFloat())/pumpRate);}//set duration of pump on to achieve desired output volume with current pump rate
    if(input.indexOf("cal") == 0){pumpRate = float(input.substring(input.indexOf(".")+1, input.length()).toFloat())/1000;}//calibrate pump rate with user input
    if(input.indexOf("goX") == 0){parseXY(input);}//Parse string XY then go to position x, y
    if(input.indexOf("ssid.") == 0){handleWifi(input);}//configure the wifi then reset the device
    if(input.indexOf("microstep") == 0){parseMicrostep(input);}//Update the microStep variable and all derivatives
    if(input.indexOf("speed") == 0){updateSpeed(input);}//set the speed in steps per second
}

// Does nothing right now just a callback fn
String processor(const String& var)
{
  debugln("Processor fn received string: " + var);
  return String();
}

//Main initialize function that starts serial connections sets pin modes etc...
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
  // Update user about wifi
  sprintf(buffer, "MAC:%s",  WiFi.macAddress().c_str());
  debugln(buffer);
  if(WiFi.isConnected())
  {
    sprintf(buffer, "IP:%s",  WiFi.localIP().toString().c_str());
  }else
  {
    sprintf(buffer, "Wifi:%s IP:%s", apssid.c_str(), WiFi.softAPIP().toString().c_str());
  }
  update_status(buffer);
}

//Setup for the x and y steppers
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

//Setup for the OLED
void initialize_OLED()
{
  Wire.begin(5, 4);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {debugln(F("SSD1306 allocation failed"));for(;;);}else
  {
    display.setTextSize(2);
    display.setTextColor(WHITE);
    // Clear the buffer.  
    update_status("XYFLY BETA");
  }
}

//Setup the over the air (OTA) update functionality
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
      SPIFFS.end();
      update_status("OTA: " + type);
      debugln("OTA update of type: " + type);
    })
    .onEnd([]() 
    {
      debugln("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) 
    {
      sprintf(buffer, "Update    Progress: %u%%\r", (progress / (total / 100)));
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

//Setup the WiFi
void initialize_wifi()
{
  read_config();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(1000);
    debug(".");
    i++;
    if(i>10){break;}
  }
}

//Setup the webserver which will be used as the user interface
void initialize_webserver()
{
  dnsServer.start(53, "*", WiFi.softAPIP());
  update_status("DNS server started");
  //On get requests do things
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {//uses the spiffs, sends the index.html on /, sends resulting the html code to the processor function
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request)
  {//uses the spiffs, sends the config.html on /, sends the resulting html code to the processor function
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
