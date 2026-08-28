#include "who_diagnostics.h"
#include <math.h>

// --- MAIN DIAGNOSTIC & TRAJECTORY EVALUATION ENGINE ---
DiagnosticResult evaluatePatientDiagnostics(const CurrentMeasurement& current, 
                                           const CloudPatientHistory& history) 
{
    DiagnosticResult result;
    
    // 0. Default Safe Initializations
    result.isValid = false;
    result.currentZScore = 0.0f;
    result.currentStatus = STATUS_NORMAL;
    result.trajectory = TRAJECTORY_BASELINE;
    result.isFaltering = false;
    result.weightVelocityGramsPerMonth = 0.0f;
    result.deltaZScore = 0.0f;

    // 1. SENSOR SANITY & CLINICAL BOUNDARY VALIDATION
    // WHO Weight-for-Length standards apply to recumbent lengths between 45.0 cm and 110.0 cm
    if (current.lengthCm < 45.0f || current.lengthCm > 110.0f || 
        current.weightKg < 1.5f   || current.weightKg > 25.0f ||
        current.ageMonths > 24) {
        return result; // Early exit on invalid physiological ranges
    }
    result.isValid = true;

    // 2. WHO LMS PARAMETER RETRIEVAL & Z-SCORE CALCULATION
    // Note: WHO WLZ index is parameterized by recumbent length and sex (0-24 months cohort)
    LMS_Params lms = getInterpolatedLMS(current.lengthCm, current.isMale);
    
    if (fabsf(lms.L) > 0.0001f) {
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

    // 4. LONGITUDINAL DELTA-Z (ΔZ) TRAJECTORY ENGINE
    if (history.hasHistory) {
        // Standard deviation shift: ΔZ = Z(t) - Z(t-1)
        result.deltaZScore = result.currentZScore - history.prevZScore;

        // Normalized growth velocity (grams / 30-day window)
        if (history.daysElapsed > 0) {
            float deltaWeightKg = current.weightKg - history.prevWeightKg;
            result.weightVelocityGramsPerMonth = (deltaWeightKg / (float)history.daysElapsed) * 30.0f * 1000.0f;
        }

        // Screening thresholds for centile trajectory drift
        if (result.deltaZScore <= -0.50f) {
            // Negative drift across growth channels -> Early Faltering Alert
            result.trajectory = TRAJECTORY_FALTERING;
            result.isFaltering = true;
        } else if (result.deltaZScore >= 0.50f) {
            // Positive drift across growth channels -> Catch-up growth / Recovery
            result.trajectory = TRAJECTORY_CATCH_UP;
            result.isFaltering = false;
        } else {
            // Tracking along expected centile curve
            result.trajectory = TRAJECTORY_STABLE;
            result.isFaltering = false;
        }
    } else {
        // No prior baseline on record
        result.trajectory = TRAJECTORY_BASELINE;
        result.isFaltering = false;
    }

    return result;
}

// --- STRING CONVERTER HELPERS ---

const char* getAcuteStatusString(AcuteStatus status) {
    switch (status) {
        case STATUS_SAM:        return "SAM";
        case STATUS_MAM:        return "MAM";
        case STATUS_NORMAL:     return "NORMAL";
        case STATUS_OVERWEIGHT: return "OVERWEIGHT";
        case STATUS_OBESE:      return "OBESE";
        default:                return "UNKNOWN";
    }
}

const char* getTrajectoryStatusString(TrajectoryStatus trajectory) {
    switch (trajectory) {
        case TRAJECTORY_BASELINE:  return "BASELINE";
        case TRAJECTORY_STABLE:    return "STABLE";
        case TRAJECTORY_FALTERING: return "FALTERING";
        case TRAJECTORY_CATCH_UP:  return "CATCH_UP";
        case TRAJECTORY_PENDING:   return "PENDING";
        default:                   return "UNKNOWN";
    }
}
