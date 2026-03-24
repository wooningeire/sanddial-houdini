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

    /// Compute particle stress tensors in-place.
    /// Updates `stressTensor` and `deformationGrad` on each particle.
    void solve(AreniteGeometry& geo, fpreal dt);

private:
    /// Particle-to-grid transfer (P2G).
    void transferToGrid(const AreniteGeometry& geo);

    /// Grid force computation and velocity update.
    void computeGridForces(fpreal dt);

    /// Grid-to-particle transfer (G2P).
    void transferToParticles(AreniteGeometry& geo, fpreal dt);
};
