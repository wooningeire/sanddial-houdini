#pragma once

#include "AreniteGeometry.h"

/// Computes wind erosion values per particle.
///
/// Wind erosion has two components:
///   - **Wind deflation**: direct dislodging of particles by wind friction.
///   - **Wind abrasion**: erosion from solid particles carried by the wind,
///     simulated via divergence-free SPH (DFSPH).
///
/// The solver maintains its own set of wind particles for the abrasion
/// simulation.
class WindSolver {
public:
    WindSolver() = default;
    ~WindSolver() = default;

    // ── Parameters ──────────────────────────────────────────────────────────
    /// Dominant wind direction (will be normalised internally).
    UT_Vector3 windDirection{1, 0, 0};

    /// Wind speed magnitude (m/s).
    fpreal windSpeed = 5.0;

    /// Turbulence intensity in [0, 1].
    fpreal turbulence = 0.2;

    /// Compute per-particle wind erosion and accumulate into
    /// `AreniteParticle::erosionValue`.
    void solve(AreniteGeometry& geo, fpreal dt);

private:
    /// Compute wind deflation for every surface particle.
    void computeDeflation(AreniteGeometry& geo);

    /// Run a DFSPH step for the wind particles and accumulate abrasion on
    /// sandstone surface particles hit.
    void computeAbrasion(AreniteGeometry& geo, fpreal dt);
};
