#include <Arduino.h>
#include <math.h>

// --- DATA STRUCTURES & ENUMS ---

enum AcuteStatus {
    STATUS_SAM,          // Severe Acute Malnutrition (Z < -3.0)
    STATUS_MAM,          // Moderate Acute Malnutrition (-3.0 <= Z < -2.0)
    STATUS_NORMAL,       // Normal / Eutrophic (-2.0 <= Z <= +2.0)
    STATUS_OVERWEIGHT,   // Overweight Risk (+2.0 < Z <= +3.0)
    STATUS_OBESE         // Obese (Z > +3.0)
};

enum FalteringReason {
    FALTERING_NONE,                  // Trajectory improving/stable (moving towards 0)
    FALTERING_TOWARDS_MALNUTRITION,  // Drifting negative away from 0
    FALTERING_TOWARDS_OBESITY        // Drifting positive away from 0
};

struct CurrentMeasurement {
    float weightKg;       // Measured weight (kg)
    float lengthCm;       // Measured length (cm)
    bool isMale;          // true = Male, false = Female
    uint8_t ageMonths;    // Patient age in completed months (0 to 24)
};

struct CloudPatientHistory {
    bool hasHistory;      // true if previous visit data exists
    float prevWeightKg;   // Weight from last visit
    float prevLengthCm;   // Length from last visit
    float prevZScore;     // Z-score from last visit
    uint16_t daysElapsed; // Days between last visit and today
};

struct DiagnosticResult {
    bool isValid;                        // Input sanity check flag
    float currentZScore;                 // Computed WLZ Z-score
    AcuteStatus currentStatus;           // Point-in-time clinical classification
    bool isFaltering;                    // High-priority trajectory alert flag
    FalteringReason falteringCause;     // Direction of drift (Obesity or Malnutrition)
    float weightVelocityGramsPerMonth; // Normalized growth rate (g/30 days)
    float deltaZScore;                   // Simple Z-score shift (Z_curr - Z_prev)
};

struct LMS_Params {
    float L;
    float M;
    float S;
};

// --- HELPER LMS INTERPOLATION (MOCK/FLASH LOOKUP) ---
LMS_Params getInterpolatedLMS(float lengthCm, bool isMale) {
    // In production, this reads PROGMEM tables and interpolates L, M, S
    LMS_Params lms;
    lms.L = -0.3521f;
    lms.M = (isMale) ? (0.15f * lengthCm - 4.2f) : (0.14f * lengthCm - 4.0f);
    lms.S = 0.0910f;
    return lms;
}

// --- MAIN DIAGNOSTIC EVALUATOR FUNCTION ---
DiagnosticResult evaluatePatientDiagnostics(const CurrentMeasurement& current, 
                                           const CloudPatientHistory& history) 
{
    DiagnosticResult result;
    
    // Default safe initializations
    result.isValid = false;
    result.currentZScore = 0.0f;
    result.currentStatus = STATUS_NORMAL;
    result.isFaltering = false;
    result.falteringCause = FALTERING_NONE;
    result.weightVelocityGramsPerMonth = 0.0f;
    result.deltaZScore = 0.0f;

    // 1. INPUT SANITY & BOUNDARY VALIDATION
    if (current.lengthCm < 45.0f || current.lengthCm > 110.0f || 
        current.weightKg < 1.5f   || current.weightKg > 25.0f) {
        return result; // Early exit on invalid sensor data
    }
    result.isValid = true;

    // 2. WHO LMS PARAMETER RETRIEVAL & Z-SCORE CALCULATION
    LMS_Params lms = getInterpolatedLMS(current.lengthCm, current.isMale);
    
    if (fabs(lms.L) > 0.0001f) {
        result.currentZScore = (powf((current.weightKg / lms.M), lms.L) - 1.0f) / (lms.L * lms.S);
    } else {
        result.currentZScore = logf(current.weightKg / lms.M) / lms.S;
    }

    // 3. POINT-IN-TIME ACUTE CLINICAL STATUS CLASSIFICATION
    if (result.currentZScore < -3.0f) {
        result.currentStatus = STATUS_SAM;
    } else if (result.currentZScore < -2.0f) {
        result.currentStatus = STATUS_MAM;
    } else if (result.currentZScore <= 2.0f) {
        result.currentStatus = STATUS_NORMAL;
    } else if (result.currentZScore <= 3.0f) {
        result.currentStatus = STATUS_OVERWEIGHT;
    } else {
        result.currentStatus = STATUS_OBESE;
    }

    // 4. LONGITUDINAL TRAJECTORY & GROWTH FALTERING ENGINE
    if (history.hasHistory && history.daysElapsed > 0) {
        // Calculate Z-Score shift and weight growth velocity
        result.deltaZScore = result.currentZScore - history.prevZScore;
        
        float deltaWeightKg = current.weightKg - history.prevWeightKg;
        result.weightVelocityGramsPerMonth = (deltaWeightKg / (float)history.daysElapsed) * 30.0f * 1000.0f;

        // Distance to median (0 SD) analysis
        float absPrev = fabsf(history.prevZScore);
        float absCurr = fabsf(result.currentZScore);
        float distanceDrift = absCurr - absPrev;

        const float DRIFT_THRESHOLD = 0.5f; // Filter out small sensor fluctuations

        // Trigger faltering ONLY if moving away from 0 SD past threshold
        if (distanceDrift >= DRIFT_THRESHOLD) {
            result.isFaltering = true;
            if (result.currentZScore < history.prevZScore) {
                result.falteringCause = FALTERING_TOWARDS_MALNUTRITION;
            } else {
                result.falteringCause = FALTERING_TOWARDS_OBESITY;
            }
        } else {
            // Convergence to 0 SD or within noise margin -> No Faltering Alert
            result.isFaltering = false;
            result.falteringCause = FALTERING_NONE;
        }
    }

    return result;
}