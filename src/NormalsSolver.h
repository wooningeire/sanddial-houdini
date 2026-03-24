#pragma once

#include "AreniteGeometry.h"

/// Computes per-particle normals for surface particles.
///
/// Surface particles are identified by belonging to a grid cell that has an
/// empty neighbor.  Normals are estimated via covariance matrices from
/// k-nearest-neighbor queries, whose eigenvectors yield the local surface
/// orientation.
class NormalsSolver {
public:
    NormalsSolver() = default;
    ~NormalsSolver() = default;

    /// Number of neighbors used in the KNN query for normal estimation.
    int kNeighbors = 16;

    /// Identify surface particles and compute their normals in-place.
    /// Updates `isSurface` and `normal` on each particle.
    void solve(AreniteGeometry& geo);

private:
    /// Build a spatial acceleration structure (e.g. grid hash) from particle
    /// positions.  Called at the start of solve().
    void buildSpatialHash(const AreniteGeometry& geo);

    /// Compute the covariance matrix from the k-nearest neighbors of the
    /// particle at index @p idx and return the estimated normal.
    UT_Vector3 estimateNormal(const AreniteGeometry& geo, exint idx);
};
