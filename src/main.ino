 /* 
 Git cmds for me:
    git config credential.helper store
    git push https://github.com/jrb07/espxyfly.git
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

// Stepper associated variables
//198720 is the number of steps to travel a full length of 250mm at 32 microsteps per step
//    Transition from step number to distance so fullLengthMM is 250mm
//To calculate the stepFactor take the microStep and divide by the max microStep (32) for example:
//    (32/microStep = stepFactor)
//    (32/2 = 16) (32/4 = 8) (32/8 = 4) (32/16 = 2) (32/32 = 1)
#include <ESP_FlexyStepper.h>
int numSubdivisions = 10;
int microStep = 32;//this is the default microstep but can be changed by the client through the server
int stepFactor = 32/microStep;// 32/32 = 1; 32/16 = 2; etc...
int fullLengthMM = 266;//this is the full length of the axis measured experimentally
int fullLengthSteps32 = 180000;//number of steps to travel a full length at 32 microsteps
int fullLengthSteps = fullLengthSteps32/stepFactor;//number of steps to travel a full length at 32 microsteps
int stepsPerMM32 = fullLengthSteps32/fullLengthMM;
int stepsPerMM = stepsPerMM32/stepFactor;//scale the stepsPerMM using the current microStep
int segmentLen = fullLengthSteps/numSubdivisions;//this is the number of steps per segment length defined by the number of subdivisions
int SPEED_IN_STEPS_PER_SECOND = 5000;
int ACCELERATION_IN_STEPS_PER_SECOND = SPEED_IN_STEPS_PER_SECOND;
int DECELERATION_IN_STEPS_PER_SECOND = SPEED_IN_STEPS_PER_SECOND;
int xTarget = fullLengthSteps;
int yTarget = fullLengthSteps;
ESP_FlexyStepper stepperX, stepperY;

// Pump
int duration = 1000;//duration in milliseconds
float pumpRate = 1;//pumping rate in mL/s
bool pumping = false;
bool fillAll = false;

// Joystick
bool joystickMode = false;
int joyX = 0;
int joyY = 0;
int threshold = 10; //Should change this to scale with a factor called sensitivity to make it more easy to understand

//BLE
#include <NimBLEDevice.h>
#define SERVICE_UUID            "d6d3eb5e-ddaf-492c-af4e-19323edf8fad"
#define CMD_CHARACTERISTIC_UUID "c57884e4-fae7-4370-81cf-6b4919b28042"
NimBLECharacteristic* pCharacteristic    = nullptr;
bool                  deviceConnected    = false;
uint32_t              value              = 0;

//Function that takes in a String and then outputs that String to all displays in this project
void update_status(String updated_status)
{
  debugln("STATUS UPDATE: ");
  debug(updated_status);
  debugln();
  debugln();
}

//Turn the pump on for a specified amount of time in milliseconds or default to the default amount of time
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

// Initialize all the periphreals and BT
void setup(){initialize_XYFLY();}

void loop(){idle();}

// Constant idle with delay that keeps parallel processing alive
void idle()
{
  delayMicroseconds(1);
  handleMain();
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
void fillAllVials()
{
  update_status("fillAllVials function entered.");
  //First check if we have reached our x target position
  if(xTarget == stepperX.getCurrentPositionInSteps())
  {//if we reached our target position then pump
    pump();
    //if we have not reached the last position then move to the next position and break
    if(xTarget < fullLengthSteps)
    {
      update_status("Moving to next X position");
      move_to_next_X();
      return;
    }
  }
  //if we have reached the final x position then execute same logic for y position
  if(yTarget == stepperY.getCurrentPositionInSteps())
  {//if we reached our target position then pump
    pump(1000);
    if(yTarget > -fullLengthSteps)
    {
      update_status("Moving to next Y position");
      move_to_next_Y();
      return;
    }
  }
  if(xTarget == fullLengthSteps && yTarget == -fullLengthSteps)
  {
    //we have reached the final position
    //go home and set fillallVials to false
    fillAll = false;
    go_home();
    return;
  }
}

//Function that is called when the BLE device is done advertising.
void onAdvComplete(NimBLEAdvertising* pAdvertising) 
{
    update_status("onAdvComplete() called.");
    if (deviceConnected) 
    {
      update_status("Device is still connected. If device disconnects then auto-restart advertising.");
      return;
    }
}

// Move the motors and search for the limit switches...
void handleMain()
{  
  //Handle homing operations before moving any steppers
  handle_homing();
  //Handle stepper movement
  handle_movement();
  /*
    Logic for processing the fillAllVials function which is essentially the main test/debug/example program
      In the future we can make the program a framework that the end user can iterate on
  */
  if(fillAll){fillAllVials();}
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
    stepperX.moveRelativeInMillimeters(2);
    update_status("X stepper is now performing a 2 mm bounce off X limit!");
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
    stepperY.moveRelativeInMillimeters(-2);
    update_status("Y stepper is now performing a -2 mm bounce off Y limit!");
    stepperY.setCurrentPositionAsHomeAndStop();
    //Reset the limit switch state
    yLimitSwitchState = 0;
  }

  //Check if we are searching for the X or Y limit aka finding home
  if(search_for_limit_X)
  {
    if(xTarget != 0)
   {//if we are not at 0 then try to set the target to 0
    update_status("Searching for X limit...");
    update_status("X target set to 0");
    xTarget = 0;
   }else if(stepperX.getCurrentPositionInSteps() == 0 && xTarget != -fullLengthSteps)
   {//if we have reached 0 but still havent found the limit then keep going further
    update_status("Initial search for X limit failure...");
    update_status("X target set to -fullLengthSteps");
    xTarget = -fullLengthSteps;
   }
  }
  if(search_for_limit_Y)
  {
    if(yTarget != 0)
    {//if we are not at 0 then try to set the target to 0
      update_status("Searching for Y limit...");
      update_status("Y target set to 0");
      yTarget = 0;
    }else if(stepperY.getCurrentPositionInSteps() == 0 && yTarget != fullLengthSteps)
   {//if we have reached 0 but still havent found the limit then keep going further
    update_status("Initial search for Y limit failure...");
    update_status("Y target set to fullLengthSteps");
    yTarget = fullLengthSteps;
   }
  }
}

//Process movement for the X and Y stepper if either are not at their target position.
void handle_movement()
{
  // If the target position has not been reached the process the x position first then the y position
  if(xTarget != stepperX.getCurrentPositionInSteps())
  {
    stepperX.processMovement();
  }
  if(yTarget != stepperY.getCurrentPositionInSteps())
  {
    stepperY.processMovement();
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
  stepperX.setCurrentPositionAsHomeAndStop();
  stepperY.setCurrentPositionAsHomeAndStop();
}

//Find the limit switches and set that position as home then bounce
void find_home()
{
  update_status("Starting search for X limit switch to acquire new X stepper home.");
  search_for_limit_X = true;
  
  update_status("Starting search for Y limit switch to acquire new Y stepper home.");
  search_for_limit_Y = true;

  //must set the target to not zero otherwise we will not home
  move_to_X_Y(-fullLengthSteps, -fullLengthSteps);//the Y target will get inverted again by the move_to_X_Y() fn
  delay(100);
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

//Turn the pump on if it off and off if it is on
void toggle_pump()
{
  if(pumping)
  {
    pumping = false; 
    update_status("Turning pump off.");
    digitalWrite(PUMP_RELAY_PIN, HIGH);
  }
  else
  {
    pumping = true; 
    update_status("Turning pump on.");
    digitalWrite(PUMP_RELAY_PIN, LOW);
  }
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
  yTarget = round(-_y);
  sprintf(buffer, "Set stepper target step position [X:%i, Y:%i]", xTarget, yTarget);
  debugln(buffer);
  stepperX.setTargetPositionInSteps(xTarget);
  stepperY.setTargetPositionInSteps(yTarget);
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

//Switch on a String value to control logic flow through the web based UI
void handle_input(String input)
{
    sprintf(buffer, "Input command received as: %s", input.c_str());
    update_status(buffer);
    if(input == "reset")                      {update_status("HARD RESET");delay(1000);ESP.restart();}//hard reset
    if(input == "stop")                       { stepperX.emergencyStop();
                                                stepperY.emergencyStop();
                                                fillAll = false;}                                     //emergency stop all operations
    if(input == "stopX")                      {stepperX.emergencyStop();}                             //emergency stop for x stepper
    if(input == "stopY")                      {stepperY.emergencyStop();}                             //emergency stop for y stepper
    if(input == "gohome")                     {go_home();}                                            //go to 0,0
    if(input == "sethome")                    {set_home();}                                           //set the current position as 0,0
    if(input == "findhome")                   {find_home();}                                          //find home
    if(input == "fillall")                    {fillAll = true;}                                       //enable async fill operations
    if(input == "stopfillall")                {fillAll = false;}                                      //halt the async fill operations
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
    if(input == "fullvial")                   {}                                                      //capture a full vial reference image
    if(input == "emptyvial")                  {}                                                      //capture an empty vial reference image
    if(input.indexOf("dur") == 0)             {duration = int(( input.substring(input.indexOf(".")+1, 
                                                                input.length()).toFloat())/pumpRate);}//set duration of pump on to achieve desired output volume with current pump rate
    if(input.indexOf("cal") == 0)             {pumpRate = float(input.substring(input.indexOf(".")+1, 
                                                                input.length()).toFloat())/1000;}     //calibrate pump rate with user input
    if(input.indexOf("goX") == 0)             {parseXY(input);}                                       //Parse string XY then go to position x, y
    if(input.indexOf("microstep") == 0)       {parseMicrostep(input);}                                //Update the microStep variable and all derivatives
    if(input.indexOf("speed") == 0)           {updateSpeed(input);}                                   //set the speed in steps per second
    if(input.indexOf("joy") == 0)             { if(input == "joy1"){joystickMode=true;}
                                                if(input == "joy0"){joystickMode=false;}}             //toggle the joystick mode
    if(input.indexOf("mmper10000steps") == 0) {parseStepCal(input);}                                  //Take the number of mm traveled in 10000 steps and use it to calculate stepsPerMM
    if(input == "stepX")                      {calibrate_steps_X();}                                  //calibrate the distance moved in 1000 steps in the positive direction from the current position using the x stepper
    if(input == "DISPLAY")                    {update_status("Received message from display-server");}// this is a test message sent to confirm that the espnow connection is alive
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
                                                                        NIMBLE_PROPERTY::READ |
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
