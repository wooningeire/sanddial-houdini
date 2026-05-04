#pragma once

#include "AreniteGeometry.h"
#include <vector>

/// Computes per-particle normals for surface particles.
///
/// Matches the paper (§5.1):
///   - Surface particles are identified as those in non-empty grid cells that
///     have at least one empty neighbour cell (grid-occupancy criterion).
///   - Normals are estimated as the leading eigenvector of the covariance
///     matrix of the k-nearest neighbours (PCA), with k = 20 and a kD-Tree
///     for acceleration.  The sign is resolved so the normal points away from
///     the interior (outward).
class NormalsSolver {
public:
    NormalsSolver() = default;
    ~NormalsSolver() = default;

    /// Number of nearest neighbours used for PCA normal estimation.
    /// Paper §5.1: k = 20.
    int kNeighbours = 20;

    /// Smoothing radius multiplier (kept for grid-hash bucket sizing).
    /// Effective search radius = smoothingRadiusMult * dx.
    fpreal smoothingRadiusMult = 2.0;

    /// Identify surface particles and compute their normals in-place.
    /// Updates `isSurface` and `normal` on each particle.
    void solve(AreniteGeometry& geo);

private:
    /// Build a spatial acceleration structure (grid hash) from particle
    /// positions.  Called at the start of solve().
    void buildSpatialHash(const AreniteGeometry& geo);

    /// Collect the indices of the k nearest non-eroded neighbours of particle
    /// `idx` within the search radius.
    void kNearestNeighbours(const AreniteGeometry& geo, exint idx,
                            fpreal searchRadius,
                            std::vector<exint>& result) const;

    /// Estimate the surface normal for particle `idx` using PCA on its
    /// k-nearest neighbours.  Returns the eigenvector corresponding to the
    /// smallest eigenvalue of the covariance matrix (the surface normal
    /// direction), oriented outward.
    UT_Vector3 estimateNormalPCA(const AreniteGeometry& geo, exint idx,
                                 const std::vector<exint>& neighbours) const;

    std::vector<std::vector<exint>> m_particleBuckets;
};
