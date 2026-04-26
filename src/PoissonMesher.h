#pragma once

#include "AreniteGeometry.h"

class GU_Detail;

/// Performs screened Poisson surface reconstruction on the simulation
/// particle cloud and writes the resulting mesh into a GU_Detail.
class PoissonMesher {
public:
    PoissonMesher() = default;
    ~PoissonMesher() = default;

    /// Reconstruct a mesh from the alive (non-eroded) surface particles
    /// in @p geo.  The resulting polygons are written into @p outputGeo.
    ///
    /// @param geo          Source particle data (positions + normals).
    /// @param outputGeo    Target Houdini detail that receives the mesh.
    /// @param depth        Octree depth for the Poisson solver (higher = finer).
    /// @param scale        Scale factor for the bounding cube (>1 adds padding).
    void reconstruct(const AreniteGeometry& geo,
                     GU_Detail* outputGeo,
                     int depth = 8,
                     float scale = 1.1f);
};
