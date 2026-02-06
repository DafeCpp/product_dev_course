#pragma once

#include <optional>

int RcInputInit();
std::optional<float> RcInputReadThrottle();
std::optional<float> RcInputReadSteering();
