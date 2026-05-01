#pragma once

#include "AreniteGeometry.h"

/// Computes per-particle normals for surface particles.
///
/// Surface particles are identified via a local SPH density estimate:
/// particles whose density falls below a threshold (indicating they sit
/// near the boundary of the particle cloud) are marked as surface.
///
/// Normals are estimated as the negative gradient of the SPH density
/// field, which varies smoothly across cell boundaries and eliminates
/// visible grid-edge artifacts.
class NormalsSolver {
public:
    NormalsSolver() = default;
    ~NormalsSolver() = default;

    /// Smoothing radius for the SPH kernel used in density / normal
    /// estimation.  Expressed as a multiplier of the grid cell size dx.
    /// The effective radius is  smoothingRadiusMult * dx.
    fpreal smoothingRadiusMult = 2.0;

    /// Identify surface particles and compute their normals in-place.
    /// Updates `isSurface` and `normal` on each particle.
    void solve(AreniteGeometry& geo);

private:
    /// Build a spatial acceleration structure (grid hash) from particle
    /// positions.  Called at the start of solve().
    void buildSpatialHash(const AreniteGeometry& geo);

    /// SPH cubic-spline kernel  W(r, h).
    fpreal kernelW(fpreal r, fpreal h) const;

    /// Gradient of the SPH cubic-spline kernel  ∇W.
    /// Returns a vector pointing from the neighbor toward the query point.
    UT_Vector3 kernelGradW(const UT_Vector3& r_vec, fpreal h) const;

    /// Estimate the surface normal for particle `idx` using the SPH
    /// density-gradient method:  n = -∇ρ / |∇ρ|.
    UT_Vector3 estimateNormal(const AreniteGeometry& geo, exint idx,
                              fpreal h) const;

    /// Compute a local SPH density for particle `idx`.
    fpreal estimateDensity(const AreniteGeometry& geo, exint idx,
                           fpreal h) const;

    std::vector<std::vector<exint>> m_particleBuckets;
};
