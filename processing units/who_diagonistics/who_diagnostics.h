#ifndef WHO_DIAGNOSTICS_H
#define WHO_DIAGNOSTICS_H

#include <Arduino.h>
#include "who_tables.h"

// --- POINT-IN-TIME ACUTE CLASSIFICATION (WHO WLZ 0-24 MONTHS) ---
enum AcuteStatus {
    STATUS_SAM,          // Severe Acute Malnutrition (Z < -3.0)
    STATUS_MAM,          // Moderate Acute Malnutrition (-3.0 <= Z < -2.0)
    STATUS_NORMAL,       // Normal / Eutrophic (-2.0 <= Z <= +2.0)
    STATUS_OVERWEIGHT,   // Overweight Risk (+2.0 < Z <= +3.0)
    STATUS_OBESE         // Obese (Z > +3.0)
};

// --- LONGITUDINAL CENTILE TRAJECTORY STATUS ---
enum TrajectoryStatus {
    TRAJECTORY_BASELINE,   // First recorded measurement (no prior visit baseline)
    TRAJECTORY_STABLE,     // Tracking along existing growth channel (-0.50 < ΔZ < +0.50)
    TRAJECTORY_FALTERING,  // Downward centile crossing / Early warning (ΔZ <= -0.50 SD)
    TRAJECTORY_CATCH_UP,   // Nutritional recovery / Upward trajectory (ΔZ >= +0.50 SD)
    TRAJECTORY_PENDING     // Stored offline; waiting for edge/cloud trajectory compute
};

// --- SENSOR INPUT PAYLOAD ---
struct CurrentMeasurement {
    float weightKg;       // Measured weight from scale driver (kg)
    float lengthCm;       // Measured length from ToF driver (cm)
    bool isMale;          // true = Male, false = Female
    uint8_t ageMonths;    // Patient age in completed months (0 to 24)
};

// --- HISTORICAL CACHE PAYLOAD (/pre_val/<ID>.csv) ---
struct CloudPatientHistory {
    bool hasHistory;      // true if previous visit record was found on SD/cloud
    float prevWeightKg;   // Weight from last visit
    float prevLengthCm;   // Length from last visit
    float prevZScore;     // WLZ Z-score from last visit Z(t-1)
    uint16_t daysElapsed; // Elapsed days between previous visit and today
};

// --- DIAGNOSTIC ENGINE OUTPUT ---
struct DiagnosticResult {
    bool isValid;                        // Input sanity check flag (range limits)
    float currentZScore;                 // Computed WHO WLZ Z-score Z(t)
    AcuteStatus currentStatus;           // Point-in-time clinical classification
    TrajectoryStatus trajectory;         // Longitudinal centile trajectory
    bool isFaltering;                    // Early warning flag (ΔZ <= -0.50 SD)
    float weightVelocityGramsPerMonth;   // Normalized growth rate (g/30 days)
    float deltaZScore;                   // Standard deviation shift: Z(t) - Z(t-1)
};

// --- CORE EVALUATION FUNCTIONS ---
DiagnosticResult evaluatePatientDiagnostics(const CurrentMeasurement& current, 
                                           const CloudPatientHistory& history);

const char* getAcuteStatusString(AcuteStatus status);
const char* getTrajectoryStatusString(TrajectoryStatus trajectory);

#endif // WHO_DIAGNOSTICS_H
