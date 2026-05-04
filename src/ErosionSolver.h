#pragma once

#include "AreniteGeometry.h"

/// Combines wind and water erosion, scales by a stress-dependent erodibility
/// factor (fabric interlocking), and updates particle viability.
///
/// When a particle's viability drops to or below zero it is marked as eroded and
/// will be handled by the DepositionSolver in the subsequent step.
class ErosionSolver {
public:
    ErosionSolver() = default;
    ~ErosionSolver() = default;

    // ── Parameters ──────────────────────────────────────────────────────────
    /// Erodibility of "weak" material (low stress).
    /// Paper Table 2: k_w = 1e-4 year^-1.
    fpreal weakErodibility = 1.0e-4;

    /// Erodibility of "strong" material (high stress / fabric interlocking).
    /// Paper Table 2: k_s = 1e-6 year^-1.  k_w >> k_s (ratio ~100x).
    fpreal strongErodibility = 1.0e-6;

    /// Stress threshold I that separates weak from strong material (Pa).
    /// Paper Eq. 2 / Table 1: 1.5–6 MPa depending on the structure.
    fpreal stressThreshold = 1.5e6;

    /// Apply combined erosion to particle viabilities.
    void solve(AreniteGeometry& geo, fpreal dt);

private:
    /// Compute a per-particle erodibility coefficient in [strongErodibility,
    /// weakErodibility] based on the Cauchy stress tensor magnitude.
    fpreal computeErodibility(const AreniteParticle& p) const;
};
