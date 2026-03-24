#pragma once

#include <UT/UT_Vector3.h>
#include <UT/UT_Array.h>
#include <UT/UT_Matrix3.h>

class GU_Detail;

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

/// A flat 3D grid used by MPM, normal estimation, flow routing, and
/// deposition.  Each cell stores a mass, momentum, and velocity for the
/// MPM transfer as well as an occupancy flag for surface detection.
struct VoxelCell {
    fpreal     mass     = 0.0;
    UT_Vector3 momentum{0, 0, 0};
    UT_Vector3 velocity{0, 0, 0};
    UT_Vector3 force{0, 0, 0};
    bool       occupied = false;
};

struct VoxelGrid {
    UT_Array<VoxelCell> cells;
    int   res[3]  = {0, 0, 0};   ///< Number of cells per axis.
    fpreal dx     = 0.1;          ///< Cell side length.
    UT_Vector3 origin{0, 0, 0};  ///< World-space origin (min corner).

    /// Allocate (or re-allocate) grid storage.
    void allocate(int rx, int ry, int rz, fpreal cellSize, const UT_Vector3& org);

    /// Reset all cell values to zero / default.
    void clear();

    /// Convert a world-space position to a grid-space index triple.
    /// Returns false if the position is outside the grid.
    bool worldToGrid(const UT_Vector3& pos, int& ix, int& iy, int& iz) const;

    /// Flat index from (ix, iy, iz).
    exint flatIndex(int ix, int iy, int iz) const {
        return (exint)ix + (exint)res[0] * ((exint)iy + (exint)res[1] * (exint)iz);
    }

    /// Whether (ix, iy, iz) is within bounds.
    bool inBounds(int ix, int iy, int iz) const {
        return ix >= 0 && ix < res[0]
            && iy >= 0 && iy < res[1]
            && iz >= 0 && iz < res[2];
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

    // ── Grid ────────────────────────────────────────────────────────────────
    VoxelGrid grid;

    // ── Grid parameters (user settings) ─────────────────────────────────────
    fpreal voxelSize = 0.1;
    UT_Vector3 domainPadding{1, 1, 1};  ///< Extra padding around particle bounds.

    // ── Helpers ─────────────────────────────────────────────────────────────
    /// Initialise particles from an array of positions.
    void initFromPositions(const UT_Array<UT_Vector3>& positions);

    /// Initialise particles from Houdini geometry (reads P, erodibility attr).
    void initFromHoudiniGeo(const GU_Detail* geo);

    /// Compute domain bounds from current particles and allocate the grid.
    void initGrid();

    /// Reset per-step transient values (erosion accumulators, etc.).
    void resetStepData();

    /// Write particle state back to a Houdini GU_Detail.
    void writeToHoudiniGeo(GU_Detail* geo) const;

    /// Return the number of non-eroded ("alive") particles.
    int aliveCount() const;
};
