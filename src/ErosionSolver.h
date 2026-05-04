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
    fpreal weakErodibility = 1.0;

    /// Erodibility of "strong" material (high stress / fabric interlocking).
    fpreal strongErodibility = 0.2;

    /// Stress threshold that separates weak from strong material.
    fpreal stressThreshold = 1.0e3;

    /// Apply combined erosion to particle viabilities.
    void solve(AreniteGeometry& geo, fpreal dt);

private:
    /// Compute a per-particle erodibility coefficient in [strongErodibility,
    /// weakErodibility] based on the Cauchy stress tensor magnitude.
    fpreal computeErodibility(const AreniteParticle& p) const;
};
