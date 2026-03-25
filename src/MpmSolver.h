#pragma once

#include "AreniteGeometry.h"

/// Computes stress tensors per particle using Moving-Least-Squares MPM.
///
/// The MPM solver transfers particle data to a background grid, computes
/// forces, and transfers updated velocities and deformation gradients back to
/// the particles.  The resulting Cauchy stress tensor is stored per particle
/// and later used by the ErosionSolver to modulate erodibility.
class MpmSolver {
public:
    MpmSolver() = default;
    ~MpmSolver() = default;

    // ── Parameters ──────────────────────────────────────────────────────────
    /// Young's modulus  (stiffness).
    fpreal youngModulus = 1.0e5;

    /// Poisson's ratio.
    fpreal poissonRatio = 0.3;

    /// Particle volume (assumed uniform).
    fpreal particleVolume = 1.0;

    /// Particle mass (assumed uniform).
    fpreal particleMass = 1.0;

    /// Compute particle stress tensors in-place.
    /// Updates `stressTensor` and `deformationGrad` on each particle.
    void solve(AreniteGeometry& geo, fpreal dt);

private:
    /// Compute the Lamé parameters (mu, lambda) from Young's modulus and
    /// Poisson's ratio.
    void computeLame(fpreal& mu, fpreal& lambda) const;

    /// Compute the quadratic B-spline weights for a particle at the given
    /// fractional grid position.  Writes 3 weights into wx, wy, wz.
    static void quadraticWeights(fpreal fx, fpreal wy[3]);

    /// Particle-to-grid transfer (P2G).
    void transferToGrid(AreniteGeometry& geo, fpreal dt, fpreal mu, fpreal lambda);

    /// Grid update: normalize by mass.
    void updateGrid(AreniteGeometry& geo, fpreal dt);

    /// Grid-to-particle transfer (G2P): update velocity, APIC C, F, and
    /// compute the Cauchy stress tensor.
    void transferToParticles(AreniteGeometry& geo, fpreal dt, fpreal mu, fpreal lambda);
};
