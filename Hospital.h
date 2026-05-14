#ifndef HOSPITAL_H
#define HOSPITAL_H

#include <QString>

// Bitmask flags for constant-time hospital capability checks.
// Example: (hospital.capabilities & required) == required
namespace HospitalCapability {
    constexpr int CT          = 1 << 0;
    constexpr int MRI         = 1 << 1;
    constexpr int OT          = 1 << 2;
    constexpr int TRAUMA      = 1 << 3;
    constexpr int BURN_UNIT   = 1 << 4;
    constexpr int CATH_LAB    = 1 << 5;
    constexpr int NEUROLOGIST = 1 << 6;
    constexpr int SURGEON     = 1 << 7;
}

enum class EmergencyType {
    GeneralEmergency = 0,
    SevereTrauma,
    HeartAttack,
    PrematureBaby,
    HeadInjury,
    BurnInjury,
    RespiratoryFailure
};

struct Hospital {
    int id = -1;
    QString name;
    QString city;
    int graphNodeId = -1;

    int erBeds = 0;
    int icuBeds = 0;
    int incubators = 0;
    int ventilators = 0;
    int waitMinutes = 0;

    int capabilities = 0;

    bool hasCapability(int requiredCapabilities) const;
    bool hasErCapacity() const;
};

#endif // HOSPITAL_H
