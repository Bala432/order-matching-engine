#pragma once

#include "Usings.h"
#include <vector>

struct LevelInfo{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;