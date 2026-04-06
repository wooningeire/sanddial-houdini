#include "NormalsSolver.h"
#include <SYS/SYS_Math.h>

void NormalsSolver::solve(AreniteGeometry& geo) {
    VoxelGrid& g = geo.grid;
    if (g.cells.size() == 0) return;

    // ── 1. Mark grid cells as occupied based on particle positions ──────
    //    (The MPM solver may have already set this, but after the last
    //     substep the grid was used for G2P.  Re-stamp occupancy here
    //     using only the base cell of each particle for a cleaner
    //     surface signal.)
    for (auto& c : g.cells)
        c.occupied = false;

    for (const auto& p : geo.particles) {
        if (p.isEroded) continue;
        int ix, iy, iz;
        if (g.worldToGrid(p.position, ix, iy, iz))
            g.cells[g.flatIndex(ix, iy, iz)].occupied = true;
    }

    // ── 2. Detect surface particles ─────────────────────────────────────
    //    A particle is on the surface if its cell has at least one
    //    empty 6-connected face neighbor (paper Sect. 5.1).
    static const int offsets[6][3] = {
        {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}
    };

    for (auto& p : geo.particles) {
        if (p.isEroded) { p.isSurface = false; continue; }

        int ix, iy, iz;
        if (!g.worldToGrid(p.position, ix, iy, iz)) {
            p.isSurface = true;  // Outside grid → treat as surface
            continue;
        }

        p.isSurface = false;
        for (int n = 0; n < 6; ++n) {
            int nx = ix + offsets[n][0];
            int ny = iy + offsets[n][1];
            int nz = iz + offsets[n][2];
            if (!g.inBounds(nx, ny, nz) ||
                !g.cells[g.flatIndex(nx, ny, nz)].occupied) {
                p.isSurface = true;
                break;
            }
        }
    }

    // ── 3. Estimate normals for surface particles ───────────────────────
    buildSpatialHash(geo);

    for (exint i = 0; i < geo.particles.size(); ++i) {
        auto& p = geo.particles[i];
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
