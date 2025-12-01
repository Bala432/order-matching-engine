#pragma once

#include "Usings.h"
#include<limits>

struct Constants{
    static const Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
};