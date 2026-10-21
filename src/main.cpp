// Placeholder so the cores3 environment links; Task 10 replaces this with the wiring.
#include <Arduino.h>

#include "ftms_server.h"

static FtmsServer server;

void setup() { server.begin("WalkingPad Bridge"); }
void loop() { delay(1000); }
