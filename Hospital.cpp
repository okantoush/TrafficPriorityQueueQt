#include "Hospital.h"

bool Hospital::hasCapability(int requiredCapabilities) const
{
    return (capabilities & requiredCapabilities) == requiredCapabilities;
}

bool Hospital::hasErCapacity() const
{
    return erBeds > 0;
}
