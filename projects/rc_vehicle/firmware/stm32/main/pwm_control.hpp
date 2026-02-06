#pragma once
#include <stdint.h>

int PwmControlInit();
int PwmControlSetThrottle(float throttle);
int PwmControlSetSteering(float steering);
void PwmControlSetNeutral();
