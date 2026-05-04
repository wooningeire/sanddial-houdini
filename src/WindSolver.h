#pragma once

#include "AreniteGeometry.h"

/// Computes wind erosion values per particle.
///
/// Wind erosion has two components:
///   - **Wind deflation**: direct dislodging of particles by wind friction,
///     modeled via the Mohr-Coulomb / Rayleigh distribution formula (Eq. 12).
///   - **Wind abrasion**: erosion from solid particles carried by the wind,
struct WindParticle {
    UT_Vector3 pos;
    UT_Vector3 vel;
    fpreal density = 0.0;
    fpreal pressure = 0.0;
};

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

    // ── Deflation parameters (Eq. 12) ───────────────────────────────────────
    /// Deflation constant k_d.
    fpreal deflationCoeff = 5.0;

    /// Internal cohesion coefficient μ_c (Pa).
    fpreal cohesion = 1e6;

    /// Dry friction coefficient μ_f.
    fpreal frictionCoeff = 0.75;

    /// Wind strength scaling α.
    fpreal windAlpha = 2e6;

    // ── Abrasion parameters (Eq. 8) ────────────────────────────────────────
    /// Abrasion constant k_a.
    fpreal abrasionCoeff = 0.1;

    /// SPH smoothing length (h).
    fpreal smoothingLength = 0.4;

    /// Wind particle mass.
    fpreal particleMass = 0.01;

    /// Rest density of wind (e.g., 1.225 kg/m³ for air).
    fpreal restDensity = 1.225;

    /// Wind viscosity.
    fpreal viscosity = 0.1;

    /// Compute per-particle wind erosion and accumulate into
    /// `AreniteParticle::erosionValue`.
    void solve(AreniteGeometry& geo, fpreal dt);

    /// Return the current set of wind particles (for visualization).
    const UT_Array<WindParticle>& getWindParticles() const { return myWindParticles; }
    void setWindParticles(const UT_Array<WindParticle>& particles) { myWindParticles = particles; }

    /// Reset the solver state (e.g. for simulation rewinds).
    void reset() { myWindParticles.clear(); }

private:
    /// Compute wind deflation for every surface particle (Eq. 12).
    void computeDeflation(AreniteGeometry& geo);

    /// Run a DFSPH step for the wind particles and accumulate abrasion on
    /// sandstone surface particles hit.
    void computeAbrasion(AreniteGeometry& geo, fpreal dt);

    // ── SPH Helpers ────────────────────────────────────────────────────────
    /// Spawn new wind particles on the upstream face of the wind domain.
    /// @p windMin / @p windMax must be the SAME box used by the cleanup
    /// pass in computeAbrasion -- otherwise emitted particles can fall
    /// outside the cleanup region and be removed on the very same step.
    void emitWindParticles(const AreniteGeometry& geo, fpreal dt,
                           const UT_Vector3& windMin,
                           const UT_Vector3& windMax);
    void updateWindParticles(const AreniteGeometry& geo, fpreal dt);
    
    /// Cubic spline kernel W(r, h).
    fpreal kernelW(fpreal r) const;
    /// Gradient of cubic spline kernel ∇W(r, h).
    UT_Vector3 kernelGradW(const UT_Vector3& r_vec) const;

    UT_Array<WindParticle> myWindParticles;

    // Wind domain expansion factor (hardcoded per user request).
    static constexpr fpreal WIND_DOMAIN_EXPANSION = 2.0;
};
