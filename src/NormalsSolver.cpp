#include "NormalsSolver.h"
#include <SYS/SYS_Math.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── SPH cubic-spline kernel ────────────────────────────────────────────────
// W(r, h) = σ * { 6(q³ - q²) + 1,   q ≤ 0.5
//               { 2(1-q)³,            0.5 < q ≤ 1
//               { 0,                  q > 1
// where q = r/h,  σ = 8 / (π h³)
fpreal NormalsSolver::kernelW(fpreal r, fpreal h) const {
    fpreal q = r / h;
    if (q >= 1.0) return 0.0;

    fpreal sigma = 8.0 / (M_PI * h * h * h);
    if (q <= 0.5) {
        return sigma * (6.0 * (q * q * q - q * q) + 1.0);
    } else {
        fpreal t = 1.0 - q;
        return sigma * 2.0 * t * t * t;
    }
}

// ── Gradient of the cubic-spline kernel ────────────────────────────────────
// ∇W = dW/dr * (r_vec / |r_vec|)
UT_Vector3 NormalsSolver::kernelGradW(const UT_Vector3& r_vec, fpreal h) const {
    fpreal r = r_vec.length();
    if (r < 1e-10 || r >= h) return UT_Vector3(0, 0, 0);

    fpreal q = r / h;
    fpreal sigma = 8.0 / (M_PI * h * h * h * h);  // extra /h from dq/dr
    fpreal dWdq;

    if (q <= 0.5) {
        dWdq = sigma * 6.0 * (3.0 * q * q - 2.0 * q);
    } else {
        fpreal t = 1.0 - q;
        dWdq = -sigma * 6.0 * t * t;
    }

    return (r_vec / r) * dWdq;
}

// ── Density estimate ───────────────────────────────────────────────────────
fpreal NormalsSolver::estimateDensity(const AreniteGeometry& geo, exint idx,
                                     fpreal h) const {
    const auto& p0 = geo.particles(idx);
    int ix, iy, iz;
    if (!geo.grid.worldToGrid(p0.position, ix, iy, iz)) return 0.0;

    int searchR = (int)std::ceil(h / geo.grid.dx);
    fpreal density = 0.0;

    for (int dz = -searchR; dz <= searchR; ++dz) {
        for (int dy = -searchR; dy <= searchR; ++dy) {
            for (int dx = -searchR; dx <= searchR; ++dx) {
                int nx = ix + dx;
                int ny = iy + dy;
                int nz = iz + dz;
                if (!geo.grid.inBounds(nx, ny, nz)) continue;

                const auto& bucket = m_particleBuckets[geo.grid.flatIndex(nx, ny, nz)];
                for (exint nIdx : bucket) {
                    fpreal r = (p0.position - geo.particles(nIdx).position).length();
                    density += kernelW(r, h);
                }
            }
        }
    }
    return density;
}

// ── Normal estimate via density gradient ───────────────────────────────────
// n = -∇ρ / |∇ρ|,  where ∇ρ(x) = Σ_j ∇W(x - xⱼ, h)
UT_Vector3 NormalsSolver::estimateNormal(const AreniteGeometry& geo, exint idx,
                                        fpreal h) const {
    const auto& p0 = geo.particles(idx);
    int ix, iy, iz;
    if (!geo.grid.worldToGrid(p0.position, ix, iy, iz)) return UT_Vector3(0, 1, 0);

    int searchR = (int)std::ceil(h / geo.grid.dx);
    UT_Vector3 gradRho(0, 0, 0);

    for (int dz = -searchR; dz <= searchR; ++dz) {
        for (int dy = -searchR; dy <= searchR; ++dy) {
            for (int dx = -searchR; dx <= searchR; ++dx) {
                int nx = ix + dx;
                int ny = iy + dy;
                int nz = iz + dz;
                if (!geo.grid.inBounds(nx, ny, nz)) continue;

                const auto& bucket = m_particleBuckets[geo.grid.flatIndex(nx, ny, nz)];
                for (exint nIdx : bucket) {
                    UT_Vector3 r_vec = p0.position - geo.particles(nIdx).position;
                    gradRho += kernelGradW(r_vec, h);
                }
            }
        }
    }

    fpreal len = gradRho.length();
    if (len < 1e-10) return UT_Vector3(0, 1, 0);

    // Normal points outward = direction of decreasing density = -∇ρ
    return -gradRho / len;
}

// ── Main solve ─────────────────────────────────────────────────────────────
void NormalsSolver::solve(AreniteGeometry& geo) {
    VoxelGrid& g = geo.grid;
    if (g.cells.size() == 0) return;

    fpreal h = smoothingRadiusMult * g.dx;
    if (h < 1e-10) h = 2.0 * g.dx;

    // ── 1. Mark grid cells as occupied ──────────────────────────────────
    //    Still needed by WindSolver for collision detection and by
    //    DepositionSolver for routing.
    for (auto& c : g.cells)
        c.occupied = false;

    for (const auto& p : geo.particles) {
        if (p.isEroded) continue;
        int ix, iy, iz;
        if (g.worldToGrid(p.position, ix, iy, iz))
            g.cells[g.flatIndex(ix, iy, iz)].occupied = true;
    }

    // ── 2. Build spatial hash for neighbor queries ──────────────────────
    buildSpatialHash(geo);

    // ── 3. Compute density at every non-eroded particle ─────────────────
    //    Auto-calibrate the surface threshold: the median density in the
    //    interior is high; particles with density below a fraction of the
    //    maximum are on the surface.
    std::vector<fpreal> densities(geo.particles.size(), 0.0);
    fpreal maxDensity = 0.0;

    for (exint i = 0; i < geo.particles.size(); ++i) {
        if (geo.particles(i).isEroded) continue;
        densities[i] = estimateDensity(geo, i, h);
        if (densities[i] > maxDensity) maxDensity = densities[i];
    }

    // Surface threshold: particles with density below 75% of max are
    // considered surface particles.  This captures the density drop-off
    // at the particle cloud boundary without requiring manual tuning.
    fpreal surfaceThreshold = maxDensity * 0.75;

    // ── 4. Detect surface particles via density threshold ───────────────
    for (exint i = 0; i < geo.particles.size(); ++i) {
        auto& p = geo.particles[i];
        if (p.isEroded) {
            p.isSurface = false;
            continue;
        }
        p.isSurface = (densities[i] < surfaceThreshold);
    }

    // ── 5. Estimate normals for surface particles ───────────────────────
    for (exint i = 0; i < geo.particles.size(); ++i) {
        auto& p = geo.particles[i];
        if (p.isSurface && !p.isEroded) {
            p.normal = estimateNormal(geo, i, h);
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
