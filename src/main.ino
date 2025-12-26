 /* 
Read the README and TODO markdown files for more information about this project.

Open the main_app.html in a browser (on any device with a browser and bluetooth) to interact with the device!

*/

// Define Serial print functions as debug functions
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)

//Define pins
#define PUMP_RELAY_PIN 18
#define LIMIT_SWITCH_PIN_X 5
#define LIMIT_SWITCH_PIN_Y 25
#define STEP_PIN_X 14
#define DIR_PIN_X 12
#define STEP_PIN_Y 13
#define DIR_PIN_Y 15
#define JOYSTICK_PIN_X 26 //USER INPUT PIN
#define JOYSTICK_PIN_Y 27 //USER INPUT PIN

//Limit switch interrupt related variables
byte xLimitSwitchState = 0;
byte yLimitSwitchState = 0;
bool check_x_limit = false;
bool check_y_limit = false;
bool search_for_limit_X = true;
bool search_for_limit_Y = true;

// Variables for parsing commands from the user
char buffer[512];
char character; 

/* 
Stepper associated variables:
Number of steps per full length = 198720 steps
                   fullLengthMM = 250 mm
                   stepsPerMM32 = 0.0013 mm/step or 1.3 micrometers/step
To calculate the stepFactor take the microStep and divide by the max microStep (32) for example:
    (32/microStep = stepFactor) this will allow us to adjust between alternate microStep settings in realtime
    (32/2 = 16) (32/4 = 8) (32/8 = 4) (32/16 = 2) (32/32 = 1) dividing by 2 or 4 etc... reduces the number of steps since they will now be larger in distance
    DERECIATED microStep was a user defined variable but now we will adjust stepper speed by user input only through FlexyStepper library functions
*/
#include <ESP_FlexyStepper.h>
int numSubdivisions = 10;//the number of segments to divide the X and Y axes into

int microStep = 32;//this is the default microstep but can be changed by the client through the server
int stepFactor = 32/microStep;// 32/32 = 1; 32/16 = 2; etc...

int fullLengthMM = 266;//this is the full length of the axis measured experimentally
int fullLengthSteps32 = 180000;//number of steps to travel a full length at 32 microsteps
int fullLengthSteps = fullLengthSteps32/stepFactor;//number of steps to travel a full length at 32 microsteps adjusted by our microstep factor delta

int stepsPerMM32 = fullLengthSteps32/fullLengthMM;
int stepsPerMM = stepsPerMM32/stepFactor;//scale the stepsPerMM using the current microStep
int segmentLen = fullLengthSteps/numSubdivisions;//this is the number of steps per segment length defined by the number of subdivisions

int SPEED_IN_STEPS_PER_SECOND = 6000;
int ACCELERATION_IN_STEPS_PER_SECOND = SPEED_IN_STEPS_PER_SECOND;
int DECELERATION_IN_STEPS_PER_SECOND = SPEED_IN_STEPS_PER_SECOND;

int xTarget = fullLengthSteps;
int yTarget = fullLengthSteps;
ESP_FlexyStepper stepperX, stepperY;

// Pump
int duration            = 1000;//duration in milliseconds
float pumpRate          = 1;//pumping rate in mL/s
bool pumping            = false;//a boolean to keep track of if we have turned the pump on or off
unsigned long pumpStart = millis();//the pump starting time to keep up with async pumping operations

// Joystick
bool joystickMode = false;
int joyX          = 0;
int joyY          = 0;
int sennsitivity  = 10;

//BLE
#include <NimBLEDevice.h>
#define SERVICE_UUID "d6d3eb5e-ddaf-492c-af4e-19323edf8fad"
#define CMD_CHARACTERISTIC_UUID "c57884e4-fae7-4370-81cf-6b4919b28042"
NimBLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
uint32_t value = 0;

/*
These variables allow for Experiments to be stored and executed at specific XY locations stored as points
  description will describe this experiment
  commands will be populated with a series of text commands
    the sets of comands and delays etc... between commands
*/
struct Experiment
{
  int experimentID = 0;
  int commandID   = 0;
  unsigned long executionStart;
  unsigned long executionEnd;
  String description;
  String commands[100];
  String title;
};

struct ExperimentManager
{
  Experiment experiments[10];//10 experiments max per manager as of now
};

ExperimentManager experimentManager;
bool executingExperiments = false;
int experimentID = 0;
int commandID = 0;

//JSON for experiment read and write to SPIFFS
JsonDocument doc;

//LittleFS to open the JSON document in our SPIFFS
#include "FS.h"
#include <LITTLEFS.h>

// Parse JSON object
void parseJSONtoExperiments()
{
  File file = fs.open("/experiments.json");
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return;
    }

    Serial.println("- read from file:");
    while(file.available()){
        Serial.write(file.read());
    }
    
  DeserializationError error = deserializeJson(doc, file);
  if (error) 
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    file.close();
    return;
  }

  // Extract values
  Serial.println(F("Response:"));
  Serial.println(doc["experiments"].as<String[]>());
  file.close();
}

//Function that takes in a String and then outputs that String to all displays in this project
void update_status(String updated_status)
{
  debugln();
  debugln("STATUS UPDATE: ");
  debug(updated_status);
  debugln();
}

// Initialize all the periphreals and BT
void setup(){initialize_XYFLY();}

void loop(){idle();}

// Constant idle with delay that keeps parallel processing alive
void idle()
{
  delayMicroseconds(1);
  handle_main();
}

// Update the accelerations, deecelerations, speeds, stepFactor, stepsPerMM, segmentLen, and set stepsPerMM for both steppers
void updateStepperVars()
{
  stepFactor = 32/microStep;
  stepsPerMM = stepsPerMM32/stepFactor;
  fullLengthSteps = fullLengthSteps32/stepFactor;//number of steps to travel a full length at 32 microsteps
  segmentLen = fullLengthSteps/numSubdivisions;
  ACCELERATION_IN_STEPS_PER_SECOND = SPEED_IN_STEPS_PER_SECOND;
  DECELERATION_IN_STEPS_PER_SECOND = SPEED_IN_STEPS_PER_SECOND;
  stepperX.setStepsPerMillimeter(stepsPerMM);
  stepperX.setSpeedInStepsPerSecond(SPEED_IN_STEPS_PER_SECOND);
  stepperX.setAccelerationInStepsPerSecondPerSecond(ACCELERATION_IN_STEPS_PER_SECOND);
  stepperX.setDecelerationInStepsPerSecondPerSecond(DECELERATION_IN_STEPS_PER_SECOND);
  stepperY.setStepsPerMillimeter(stepsPerMM);
  stepperY.setSpeedInStepsPerSecond(SPEED_IN_STEPS_PER_SECOND);
  stepperY.setAccelerationInStepsPerSecondPerSecond(ACCELERATION_IN_STEPS_PER_SECOND);
  stepperY.setDecelerationInStepsPerSecondPerSecond(DECELERATION_IN_STEPS_PER_SECOND);
}

//Tell the steppers to move to each position and remove each position that has been filled from the list
void executeExperiments()
{
  update_status("Executing all stored experiments...");
  Experiment targetExp = experimentManager.experiments[experimentID];
  String currentCommand = targetExp.commands[commandID];
  handle_input(currentCommand);
  if(commandID >= 100)
  {
    experimentID = experimentID + 1;
    commandID = 0;
  }
  if(experimentID < 10 && currentCommand != experimentManager.experiments[10].commands[100]){executingExperiments = true;}else{executingExperiments = false;}
}

void handleCommandSequence(String input)
{
  update_status("Received command sequence of : ");
  update_status(input);
}

//Function that is called when the BLE device is done advertising.
void onAdvComplete(NimBLEAdvertising* pAdvertising) 
{
    update_status("onAdvComplete() called.");
    if (deviceConnected) 
    {
      update_status("Device is still connected. If device disconnects then it will auto-restart advertising.");
      return;
    }
}

// Move the motors and search for the limit switches...
void handle_main()
{  
  //Handle async stepper movement
  handle_movement();
  //Handle async experiment execution
  handle_experiments();
  //Handle async pumping
  handle_pumping();
}

// Check for limit switch flags and search for limit switches if flag is true/1
void handle_homing()
{
//check the X limit switch flag conditions
  if(xLimitSwitchState == 1)
  {
    update_status("X limit switch interrupt triggered for HIGH trigger state.");
    stepperX.stopJogging();
    stepperX.setCurrentPositionAsHomeAndStop();
    search_for_limit_X = false;
    update_status("X home set. Search finished.");
    stepperX.moveRelativeInMillimeters(11);
    update_status("X stepper is now performing an 11 mm bounce off X limit!");
    stepperX.setCurrentPositionAsHomeAndStop();
    //Reset the limit switch state
    xLimitSwitchState = 0;
  }

  //Check the Y limit switch flag conditions
  if(yLimitSwitchState == 1)
  {
    update_status("Y limit switch interrupt triggered for HIGH trigger state.");
    stepperY.stopJogging();
    stepperY.setCurrentPositionAsHomeAndStop();
    search_for_limit_Y = false;
    update_status("Y home set. Search finished.");
    stepperY.moveRelativeInMillimeters(-13);
    update_status("Y stepper is now performing a -13 mm bounce off Y limit!");
    stepperY.setCurrentPositionAsHomeAndStop();
    //Reset the limit switch state
    yLimitSwitchState = 0;
  }

  //if we have gotten to the perceived home position but are still searching for home then call the find_home function again
  if(stepperX.getCurrentPositionInSteps() == 0 && search_for_limit_X)
  {
    find_home();
  }
  if(stepperY.getCurrentPositionInSteps() == 0 &&  search_for_limit_Y)
  {
    find_home();
  }

}

//Process movement for the X and Y stepper if either are not at their target position.
void handle_movement()
{
  //Handle homing operations before moving any steppers
  handle_homing();
  // If the target position has not been reached the process the x position first then the y position
  if(xTarget != stepperX.getCurrentPositionInSteps())
  {
    stepperX.processMovement();
    //if we are moving faster than 6000 steps per second then we need to move one stepper at a time to prevent overpower
    if(SPEED_IN_STEPS_PER_SECOND >= 6000)
    {
      return;
    }
  }
  if(yTarget != stepperY.getCurrentPositionInSteps())
  {
    stepperY.processMovement();
  }
}

void handle_experiments()
{
  /*
    Logic for processing the executeExperiments function which is essentially the main test/debug/example program
      In the future we can make the program a framework that the end user can iterate on
  */
  if(executeExperiments)
  {
    update_status("Executing experiments.");
  }
}

void handle_pumping()
{
  unsigned long timeElapsed = millis()-pumpStart;
  if(timeElapsed >= duration)
  {
    if(pumping)
    {
      pumping = false; 
      update_status("Turning pump off.");
      digitalWrite(PUMP_RELAY_PIN, HIGH);
    }
  }else
  {
    if(!pumping)
    {
      pumping = true;
      update_status("Turning pump on.");
      digitalWrite(PUMP_RELAY_PIN, LOW);
    }
  }
}

//Interrupt for the X limit switch; has to be short to avoid watchdog timer timeouts so will use a flag
void x_limit_switch_interrupt_rising()
{
  xLimitSwitchState = 1;
}

//Interrupt for the Y limit switch; has to be short to avoid watchdog timer timeouts so will use a flag
void y_limit_switch_interrupt_rising()
{
  yLimitSwitchState = 1;
}

//Tell both steppers to go to the Home position or x = 0 and y = 0
//THIS FUNCTION IS NOW DEPRECIATED AND WILL TRIGGER THE FIND HOME FUNCTION EVERY TIME
void go_home()
{
  update_status("Going home by triggering find_home();");
  find_home();
  // move_to_X_Y(0, 0);
  // delay(100);
}

//Set the current position as the Home position (x = 0, y = 0)
void set_home()
{       
  update_status("Setting home...");
  search_for_limit_X = false;
  search_for_limit_Y = false;
  stepperX.setCurrentPositionAsHomeAndStop();
  xTarget = 0;
  stepperY.setCurrentPositionAsHomeAndStop();
  yTarget = 0;
}

//Find the limit switches and set that position as home then bounce
void find_home()
{
  update_status("Starting search for X limit switch to acquire new X stepper home.");
  search_for_limit_X = true;
  
  update_status("Starting search for Y limit switch to acquire new Y stepper home.");
  search_for_limit_Y = true;

  // Set the perceived stepper positions to the end of each axis of travel then tell them to go to the opposite end of the inverted axis
  stepperX.setCurrentPositionInSteps(fullLengthSteps);
  stepperY.setCurrentPositionInSteps(-fullLengthSteps);

  //Set to zero and if not home then find home again
  move_to_X_Y(0, 0);
  delay(100);
}

//handle the joystick toggling using user input in form of String with a 0 for off or a 1 for on
void handleJoy(String input)
{
   if(input == "joy1")      { joystickMode =  true;   update_status("Joystick engaged");         }
   else if(input == "joy0") { joystickMode =  false;  update_status("Joystick disengaged");      }
   else                     {                         update_status("Joystick input CORRUPTED"); }
}

//Take 10000 steps to determine the distance per 10000 steps
void calibrate_steps_X()
{
  stepperX.setTargetPositionInSteps(stepperX.getCurrentPositionInSteps()+10000);
}

// Receive user feedback to determine the number of mm traveled by 1000 steps
void parseStepCal(String input)
{
  float mmper10000steps = float(input.substring(input.indexOf("mmper1000steps")+14, input.length()).toFloat());
  stepsPerMM32 = 1/mmper10000steps;
  updateStepperVars();
}

//Use a specified amount of ON time (10s) to calibrate the amount of fluid dispensed in that time period
void calibrate_pump()
{
  
  digitalWrite(PUMP_RELAY_PIN, LOW);
  delay(10000);
  digitalWrite(PUMP_RELAY_PIN, HIGH);
}

void calibrate_pumpRate(String input)
{
  pumpRate = float(input.substring(input.indexOf(".")+1, input.length()).toFloat())/1000;
}

//Turn the pump on for a specified amount of time in milliseconds or default to the default amount of time
// DEPRECIATED USE ASYNC TOGGLING WITH TIMESTAMPS INSTEAD
void pump(float _duration = 0)
{
  //if no custom duration is sent then set custom duration to default duration
  if(_duration <= 0){_duration = duration;}
  sprintf(buffer, "Dispensing for: %f ms", _duration);
  update_status(buffer);
  digitalWrite(PUMP_RELAY_PIN, LOW);
  delay(_duration);
  digitalWrite(PUMP_RELAY_PIN, HIGH);
}

//Turn the pump on if it off and off if it is on
void toggle_pump()
{
  update_status("Call to toggle_pump() is DEPRICIATED");
}

void pump_volume(String input)
{
  duration = int(( input.substring(input.indexOf(".")+1, input.length()).toFloat())/pumpRate);
  pumpStart = millis();
}

//Take a string input and parse it into a target x and y position
void parseXY(String input)
{
  float goX = input.substring(input.indexOf("goX")+3, input.indexOf("goY")).toFloat();
  goX = goX/100;
  goX = goX*fullLengthSteps;
  float goY = input.substring(input.indexOf("goY")+3, input.length()).toFloat();
  goY = goY/100;
  goY = goY*fullLengthSteps;
  move_to_X_Y(goX, goY);
}

//Move steppers to a specific point within the limits of the x and y axes
void move_to_X_Y(float _x, float _y)
{
  xTarget = round(_x);
  yTarget = round(_y);
  sprintf(buffer, "Set stepper target step position [X:%i, Y:%i]", xTarget, -yTarget);
  debugln(buffer);
  stepperX.setTargetPositionInSteps(xTarget);
  stepperY.setTargetPositionInSteps(-yTarget);
}

//Move to the previous x position
void move_to_prev_X()
{
  float X_ = constrain(xTarget-segmentLen, 0, fullLengthSteps);
  move_to_X_Y(X_, yTarget);
}

//Move to the next x position
void move_to_next_X()
{
  float X_ = constrain(xTarget+segmentLen, 0, fullLengthSteps);
  move_to_X_Y(X_, yTarget);
}

//Move to the previous y position
void move_to_prev_Y()
{
  float Y_ = constrain(yTarget-segmentLen, -fullLengthSteps, 0);
  move_to_X_Y(xTarget, Y_);
}

//Move to the next y position
void move_to_next_Y()
{
  float Y_ = constrain(yTarget+segmentLen, -fullLengthSteps, 0);
  move_to_X_Y(xTarget, Y_);
}

void parseMicrostep(String input)
{
  microStep = input.substring(input.indexOf("microstep")+9, input.length()).toInt();
  updateStepperVars();
}

void updateSpeed(String input)
{
  SPEED_IN_STEPS_PER_SECOND = input.substring(input.indexOf("speed")+5, input.length()).toInt();
  updateStepperVars();
}

void emergencyStop()
{
  stepperX.emergencyStop();
  stepperY.emergencyStop();
  executingExperiments = false;
}

void resetDevice()
{
  update_status("HARD RESET");
  delay(1000);
  ESP.restart();
}

/*
Description:  Passive structure for commands/keywords sent from the user, or by instruction sets, to be converted to specific function calls.
String input: A named String variable that represents commands from the user or instruction sets.
Flow:         Parses the String input using if logic flow to identify commands and execute functions in response to specific keywords.
*/
void handle_input(String input)
{
    sprintf(buffer, "Input command received as: %s", input.c_str());
    update_status(buffer);
    if(input == "sequence")                   {handleCommandSequence(input);}                         //handle user defined command sequences as custom experiments
    if(input == "reset")                      {resetDevice();}                                        //hard reset
    if(input == "stop")                       {emergencyStop();}                                      //emergency stop all operations
    if(input == "stopX")                      {stepperX.emergencyStop();}                             //emergency stop for x stepper
    if(input == "stopY")                      {stepperY.emergencyStop();}                             //emergency stop for y stepper
    if(input == "gohome")                     {go_home();}                                            //go to 0,0
    if(input == "sethome")                    {set_home();}                                           //set the current position as 0,0
    if(input == "findhome")                   {find_home();}                                          //find home
    if(input == "startexperiments")           {executingExperiments = true;}                          //enable async fill operations
    if(input == "endexperiments")             {executingExperiments = false;}                         //halt the async fill operations
    if(input == "dispense")                   {toggle_pump();}                                        //turn the pump on/off
    if(input == "pump")                       {pump(1000);}                                           //turn the pump on for 1 second then off
    if(input == "nextX")                      {move_to_next_X();}                                     //move to the next X position
    if(input == "prevX")                      {move_to_prev_X();}                                     //move to the last X position
    if(input == "nextY")                      {move_to_next_Y();}                                     //move to the next Y position
    if(input == "prevY")                      {move_to_prev_Y();}                                     //move to the last Y position
    if(input == "right")                      {stepperX.startJogging(-1);}                            //move x stepper in positive direction
    if(input == "left")                       {stepperX.startJogging(1);}                             //move x stepper in negative direction
    if(input == "up")                         {stepperY.startJogging(-1);}                            //move y stepper in positive direction
    if(input == "down")                       {stepperY.startJogging(1);}                             //move y stepper in negative direction
    if(input == "pump1s")                     {calibrate_pump();}                                     //activate the pump for one second
    if(input.indexOf("volume") == 0)          {pump_volume(input);}                                   //output volume of liquid using peristaltic pump
    if(input.indexOf("calibrate") == 0)       {calibrate_pumpRate(input);}                            //calibrate pump rate in milliliters per second with user input
    if(input.indexOf("goX") == 0)             {parseXY(input);}                                       //Parse string XY then go to position x, y
    if(input.indexOf("microstep") == 0)       {parseMicrostep(input);}                                //Update the microStep variable and all derivatives
    if(input.indexOf("speed") == 0)           {updateSpeed(input);}                                   //set the speed in steps per second
    if(input.indexOf("joy") == 0)             {handleJoy(input);}                                     //toggle the joystick mode
    if(input.indexOf("mmper10000steps") == 0) {parseStepCal(input);}                                  //Take the number of mm traveled in 10000 steps and use it to calculate stepsPerMM
    if(input == "stepX")                      {calibrate_steps_X();}                                  //calibrate the distance moved in 1000 steps in the positive direction from the current position using the x stepper
    if(input == "DISPLAY")                    {update_status("Received message from display-server");}// this is a test message sent to confirm that the espnow connection is alive
    if(input == "delay")                      {delay(1000);}                                          // delay for 1 second when prompted
}

//Main initialize function that starts serial connections sets pin modes etc...
void initialize_XYFLY()
{
  // Start and init serial connection
  Serial.begin(115200);
  // Stepper setup
  initialize_steppers_and_pump();
  //Initialize the BLE functions
  initialize_BLE();
  // File system setup
  initialize_FS();
}

bool initialize_FS()
{
  if(!LITTLEFS.begin())
  {
    update_status("LITTLEFS Mount Failed");
    return false;
  }
  parseJSONtoExperiments();
  return true;
}

//Setup for the x and y steppers and the pump
void initialize_steppers_and_pump()
{
  stepperX.connectToPins(STEP_PIN_X, DIR_PIN_X);
  stepperX.startAsService(2);
  stepperX.setCurrentPositionInSteps(fullLengthSteps);
  // stepperX.setDirectionToHome(1);
  update_status("X stepper service started and position assumed as distal end");
  pinMode(LIMIT_SWITCH_PIN_X, INPUT_PULLUP);
  update_status("X limit switch set to input pull up");
  attachInterrupt(digitalPinToInterrupt(LIMIT_SWITCH_PIN_X), x_limit_switch_interrupt_rising, RISING);
  update_status("X limit switch interrupt attached");

  stepperY.connectToPins(STEP_PIN_Y, DIR_PIN_Y);
  stepperY.startAsService(2);
  stepperY.setCurrentPositionInSteps(-fullLengthSteps);
  // stepperY.setDirectionToHome(-1);
  update_status("Y stepper service started and position assumed as distal end");
  pinMode(LIMIT_SWITCH_PIN_Y, INPUT_PULLUP);
  update_status("Y limit switch set to input pull up");
  attachInterrupt(digitalPinToInterrupt(LIMIT_SWITCH_PIN_Y), y_limit_switch_interrupt_rising, RISING);
  update_status("Y limit switch interrupt attached");

  updateStepperVars();
  update_status("Stepper variables assigned after generation");

  //Initialize the pump (not enough code to put in own function yet...)
  pinMode(PUMP_RELAY_PIN, OUTPUT);
}

class serverCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override 
    { 
      Serial.println("New BLE device connected.");
      deviceConnected = true; 
      NimBLEDevice::stopAdvertising();
      Serial.println("Advertising stopped.");
    };

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override 
    {
        Serial.println("BLE device disconnected.");
        deviceConnected = false;
        NimBLEDevice::startAdvertising();
        Serial.println("Advertising started.");
    };
} serverCallbacks;

class pCallbacks : public NimBLECharacteristicCallbacks 
{
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) 
  {
    Serial.println("Characteristic onWrite triggered.");
    String cmd = pCharacteristic->getValue();
    if (cmd.length() > 0) 
    {
      Serial.println("Characteristic event, written: ");
      Serial.println(cmd); // Print the command
      handle_input(cmd);
    }
  };
  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo)
  {
      Serial.println("Characteristic onRead triggered.");
  };
} pCallbacks;

void initialize_BLE()
{
  update_status("BLE starting...");
  NimBLEDevice::init("XYFLY");

  if(NimBLEDevice::isInitialized()){update_status("BLE started.");}
  //Attempt to set the dBm to +3
  if(NimBLEDevice::setPower(3)){update_status("BLE dBm set to +3");} 
  // NimBLEDevice::setSecurityAuth(true, true, true);
  // NimBLEDevice::setSecurityPasskey(124568);
  // NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  NimBLEServer*         pServer                  = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);
  pServer->advertiseOnDisconnect(true);
  NimBLEService*        pService                 = pServer->createService(SERVICE_UUID);
  NimBLECharacteristic* pCharacteristic          = pService->createCharacteristic(CMD_CHARACTERISTIC_UUID, 
                                                                                  NIMBLE_PROPERTY::READ,
                                                                                  NIMBLE_PROPERTY::WRITE);
  pCharacteristic->setCallbacks(&pCallbacks);
  pService->start();
  pCharacteristic->setValue("BLE_STARTED");

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  // Allow anyone to scan and connect (very unsafe) FIX THIS TO A PIN SECURE SERVER ASAP
  // This may be okay since we can only connect to 1 device at a time
  pAdvertising->setScanFilter(false, false);
  pAdvertising->setName("XYFLYBLE");
  pAdvertising->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  pAdvertising->enableScanResponse(false);
  pAdvertising->setAdvertisingCompleteCallback(onAdvComplete);
  pAdvertising->start();
}
