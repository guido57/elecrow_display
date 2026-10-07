#pragma once

#include <stdint.h>

// Start is inclusive, end is exclusive; equal times mean an empty period.
constexpr bool night_period_contains(uint16_t minute, uint16_t start, uint16_t end)
{
    return start < end ? minute >= start && minute < end
         : start > end ? minute >= start || minute < end
         : false;
}
