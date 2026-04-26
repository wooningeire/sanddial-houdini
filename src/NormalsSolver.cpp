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

void NormalsSolver::buildSpatialHash(const AreniteGeometry& geo) {
    m_particleBuckets.clear();
    m_particleBuckets.resize(geo.grid.cells.entries());
    for (exint i = 0; i < geo.particles.entries(); ++i) {
        const auto& p = geo.particles(i);
        if (p.isEroded) continue;
        int ix, iy, iz;
        if (geo.grid.worldToGrid(p.position, ix, iy, iz)) {
            m_particleBuckets[geo.grid.flatIndex(ix, iy, iz)].push_back(i);
        }
    }
}

UT_Vector3 NormalsSolver::estimateNormal(const AreniteGeometry& geo, exint idx) {
    const auto& p0 = geo.particles(idx);
    int ix, iy, iz;
    if (!geo.grid.worldToGrid(p0.position, ix, iy, iz)) return UT_Vector3(0, 1, 0);

    // Collect neighbors from 3x3x3 cell neighborhood
    std::vector<exint> neighbors;
    neighbors.reserve(64);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = ix + dx;
                int ny = iy + dy;
                int nz = iz + dz;
                if (geo.grid.inBounds(nx, ny, nz)) {
                    const auto& bucket = m_particleBuckets[geo.grid.flatIndex(nx, ny, nz)];
                    neighbors.insert(neighbors.end(), bucket.begin(), bucket.end());
                }
            }
        }
    }

    if (neighbors.size() < 3) return UT_Vector3(0, 1, 0);

    // Compute local centroid. The vector from centroid to particle 
    // provides a robust estimate for the outward-pointing surface normal.
    UT_Vector3 centroid(0, 0, 0);
    for (exint n_idx : neighbors) {
        centroid += geo.particles(n_idx).position;
    }
    centroid /= (fpreal)neighbors.size();

    UT_Vector3 normal = p0.position - centroid;
    fpreal len = normal.length();
    if (len < 1e-6) {
        // If particle is at centroid, fallback to UP (common for flat floors)
        return UT_Vector3(0, 1, 0);
    }
    
    normal /= len;
    return normal;
}
