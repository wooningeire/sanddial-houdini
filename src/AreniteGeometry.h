#pragma once

#include <UT/UT_Vector3.h>
#include <UT/UT_Array.h>
#include <UT/UT_Matrix3.h>

/// Stores the per-particle simulation state for the Arenite erosion simulation.
struct AreniteParticle {
    UT_Vector3 position{0, 0, 0};
    UT_Vector3 velocity{0, 0, 0};
    UT_Vector3 normal{0, 1, 0};

    /// Cauchy stress tensor (from MPM).
    UT_Matrix3  stressTensor;

    /// Deformation gradient (for MPM).
    UT_Matrix3  deformationGrad;

    /// User-paintable erodibility coefficient in [0, 1].
    fpreal      erodibility = 1.0;

    /// Accumulated erosion "viability". Starts at 1; the particle is eroded
    /// when this falls below 0.
    fpreal      viability = 1.0;

    /// Combined wind + water erosion accumulated this step.
    fpreal      erosionValue = 0.0;

    /// Whether this particle is on the surface (has an empty neighbor cell).
    bool        isSurface = false;

    /// Whether this particle has been eroded (viability <= 0).
    bool        isEroded = false;

    AreniteParticle() {
        stressTensor.identity();
        deformationGrad.identity();
    }
};

/// Container that owns all particle data and the voxel grids used by the
/// simulation.  Helper solvers read from / write to this structure each step.
class AreniteGeometry {
public:
    AreniteGeometry() = default;
    ~AreniteGeometry() = default;

    // ── Particle data ───────────────────────────────────────────────────────
    UT_Array<AreniteParticle> particles;

    // ── Grid parameters ─────────────────────────────────────────────────────
    /// Voxel side length shared by MPM, normal-estimation, and flow grids.
    fpreal voxelSize = 0.1;

    /// World-space origin of the simulation domain.
    UT_Vector3 domainOrigin{0, 0, 0};

    /// Number of grid cells along each axis.
    int gridResolution[3] = {64, 64, 64};

    // ── Helpers ─────────────────────────────────────────────────────────────
    /// Initialise particles from an array of positions (e.g. scattered from
    /// an input mesh).
    void initFromPositions(const UT_Array<UT_Vector3>& positions);

    /// Reset per-step transient values (erosion accumulators, etc.) before a
    /// new simulation step.
    void resetStepData();

    /// Return the number of non-eroded ("alive") particles.
    int aliveCount() const;
};
