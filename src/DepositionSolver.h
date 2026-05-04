#pragma once

#include "AreniteGeometry.h"
#include <vector>

/// Per-cell data used by the deposition algorithm.
struct DepCell {
    // Set during identifySurfaceCells:
    bool   isSurface      = false;
    fpreal proxyElevation = 0.0f;  ///< Average Y of surface particles in cell.
    fpreal averageSlope   = 0.0f;  ///< Angle between avg normal and up (radians).
    bool   isStable       = false; ///< slope < stableSlopeThreshold.

    // Set during assignReceivers:
    int    receiverIdx    = -1;    ///< Index of the receiver cell (-1 = none).

    // Set during buildDepositionPointers:
    int    pIdx           = -1;    ///< Final deposition target after pointer-jumping.

    // Updated during depositParticles:
    fpreal depositTop     = 0.0f;  ///< Current top of the deposit pile in this cell.
    int    depositCount   = 0;     ///< Number of particles deposited here this step.
};

/// Implements paper §7: gravitational deposition of eroded particles.
///
/// Algorithm (verbatim from paper):
///   1. Identify stable surface cells (slope < angle of repose).
///   2. For each surface cell c, assign a receiver: the next cell a particle
///      would flow to (paper Fig. 4 three-case rule from §6.2).
///   3. Set p(c) = receiver(c).  Apply pointer jumping p(c) = p(p(c)) for
///      log2(n_surface) iterations to find the nearest downward stable cell.
///   4. Each eroded particle looks up the surface cell it came from, reads
///      p(c), and is placed at a random 2D position within that stable cell
///      at the cell's proxy elevation z (which increments per deposited
///      particle to maintain target density).
///   5. The grid is shifted by a random per-step offset to avoid staircase
///      artifacts (paper §7 last paragraph).
class DepositionSolver {
public:
    DepositionSolver() = default;
    ~DepositionSolver() = default;

    /// Maximum slope (radians) below which a cell is considered stable.
    /// Paper calls this the "angle of repose" / talus slope.
    fpreal stableSlopeThreshold = 0.5f;

    /// Viability assigned to freshly deposited sediment particles.
    fpreal sedimentViability = 0.05f;

    void solve(AreniteGeometry& geo, fpreal dt, int frame);

private:
    void identifySurfaceCells(const AreniteGeometry& geo, const UT_Vector3& gridOffset);
    void assignReceivers      (const AreniteGeometry& geo);
    void buildDepositionPointers();
    void depositParticles     (AreniteGeometry& geo, int frame, const UT_Vector3& gridOffset);

    std::vector<DepCell> m_cells;
};
