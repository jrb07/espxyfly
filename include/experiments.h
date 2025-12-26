/*
    Implements structures to capture, save, and read user defined experiments
*/

#include <ArduinoJson.h>

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