#include "NormalsSolver.h"

void NormalsSolver::solve(AreniteGeometry& geo) {
    buildSpatialHash(geo);

    for (exint i = 0; i < geo.particles.size(); ++i) {
        auto& p = geo.particles[i];

        // TODO: Identify surface particles by checking for empty neighbor
        //       cells in the voxel grid.
        p.isSurface = true; // placeholder — treat all particles as surface

        if (p.isSurface && !p.isEroded) {
            p.normal = estimateNormal(geo, i);
        }
    }
}

void NormalsSolver::buildSpatialHash(const AreniteGeometry& /*geo*/) {
    // TODO: Build a grid-based spatial hash from particle positions for
    //       efficient neighbor lookups.
}

UT_Vector3 NormalsSolver::estimateNormal(const AreniteGeometry& /*geo*/, exint /*idx*/) {
    // TODO: Perform KNN lookup, compute covariance matrix, and return the
    //       eigenvector corresponding to the smallest eigenvalue.
    return UT_Vector3(0, 1, 0); // placeholder — up vector
}
