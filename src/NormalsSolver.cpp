#include "NormalsSolver.h"
#include <SYS/SYS_Math.h>
#include <algorithm>
#include <numeric>
#include <cmath>

// ── Spatial hash ───────────────────────────────────────────────────────────
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

// ── k-nearest neighbours ───────────────────────────────────────────────────
void NormalsSolver::kNearestNeighbours(const AreniteGeometry& geo,
                                       exint idx,
                                       fpreal searchRadius,
                                       std::vector<exint>& result) const {
    result.clear();
    const auto& p0 = geo.particles(idx);

    int ix, iy, iz;
    if (!geo.grid.worldToGrid(p0.position, ix, iy, iz)) return;

    int searchR = (int)std::ceil(searchRadius / geo.grid.dx);

    // Collect all candidates within the search radius.
    std::vector<std::pair<fpreal, exint>> candidates;
    for (int dz = -searchR; dz <= searchR; ++dz) {
        for (int dy = -searchR; dy <= searchR; ++dy) {
            for (int dx = -searchR; dx <= searchR; ++dx) {
                int nx = ix + dx;
                int ny = iy + dy;
                int nz = iz + dz;
                if (!geo.grid.inBounds(nx, ny, nz)) continue;

                const auto& bucket =
                    m_particleBuckets[geo.grid.flatIndex(nx, ny, nz)];
                for (exint nIdx : bucket) {
                    if (nIdx == idx) continue;
                    fpreal d2 = (p0.position - geo.particles(nIdx).position)
                                    .length2();
                    if (d2 <= searchRadius * searchRadius)
                        candidates.emplace_back(d2, nIdx);
                }
            }
        }
    }

    // Partial sort to get the k closest.
    int k = SYSmin((int)candidates.size(), kNeighbours);
    std::partial_sort(candidates.begin(), candidates.begin() + k,
                      candidates.end());
    result.reserve(k);
    for (int i = 0; i < k; ++i)
        result.push_back(candidates[i].second);
}

// ── PCA normal estimation ──────────────────────────────────────────────────
// Computes the covariance matrix of the neighbour positions (centred on the
// query particle) and returns the eigenvector for the smallest eigenvalue,
// which is the surface normal direction.  Uses the power-iteration / Jacobi
// method for the 3×3 symmetric eigenproblem.
//
// Sign convention: the normal is flipped so it points away from the centroid
// of all neighbours (outward-facing).
UT_Vector3 NormalsSolver::estimateNormalPCA(
        const AreniteGeometry& geo,
        exint idx,
        const std::vector<exint>& neighbours) const {

    if (neighbours.empty()) return UT_Vector3(0, 1, 0);

    const UT_Vector3& p0 = geo.particles(idx).position;

    // Compute centroid of neighbours.
    UT_Vector3 centroid(0, 0, 0);
    for (exint nIdx : neighbours)
        centroid += geo.particles(nIdx).position;
    centroid /= (fpreal)neighbours.size();

    // Build 3×3 covariance matrix C = Σ (pi - centroid)(pi - centroid)^T.
    // Stored as upper triangle: c[0]=xx, c[1]=xy, c[2]=xz,
    //                           c[3]=yy, c[4]=yz, c[5]=zz.
    double C[6] = {0, 0, 0, 0, 0, 0};
    for (exint nIdx : neighbours) {
        UT_Vector3 d = geo.particles(nIdx).position - centroid;
        C[0] += d.x() * d.x();
        C[1] += d.x() * d.y();
        C[2] += d.x() * d.z();
        C[3] += d.y() * d.y();
        C[4] += d.y() * d.z();
        C[5] += d.z() * d.z();
    }

    // Jacobi iteration for symmetric 3×3 eigendecomposition.
    // We want the eigenvector for the smallest eigenvalue.
    double a[3][3] = {
        {C[0], C[1], C[2]},
        {C[1], C[3], C[4]},
        {C[2], C[4], C[5]}
    };
    double v[3][3] = {{1,0,0},{0,1,0},{0,0,1}}; // eigenvectors (columns)

    for (int iter = 0; iter < 50; ++iter) {
        // Find the largest off-diagonal element.
        int p = 0, q = 1;
        double maxVal = std::abs(a[0][1]);
        if (std::abs(a[0][2]) > maxVal) { maxVal = std::abs(a[0][2]); p = 0; q = 2; }
        if (std::abs(a[1][2]) > maxVal) { maxVal = std::abs(a[1][2]); p = 1; q = 2; }
        if (maxVal < 1e-12) break;

        double theta = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
        double c = std::cos(theta);
        double s = std::sin(theta);

        // Apply Jacobi rotation.
        double ap[3], aq[3];
        for (int i = 0; i < 3; ++i) {
            ap[i] = c * a[p][i] - s * a[q][i];
            aq[i] = s * a[p][i] + c * a[q][i];
        }
        for (int i = 0; i < 3; ++i) {
            a[p][i] = a[i][p] = ap[i];
            a[q][i] = a[i][q] = aq[i];
        }
        a[p][p] = c * c * a[p][p] + s * s * a[q][q] - 2 * s * c * a[p][q];
        a[q][q] = s * s * a[p][p] + c * c * a[q][q] + 2 * s * c * a[p][q];
        a[p][q] = a[q][p] = 0.0;

        // Accumulate eigenvectors.
        for (int i = 0; i < 3; ++i) {
            double vp = c * v[i][p] - s * v[i][q];
            double vq = s * v[i][p] + c * v[i][q];
            v[i][p] = vp;
            v[i][q] = vq;
        }
    }

    // Find the index of the smallest eigenvalue.
    int minIdx = 0;
    if (a[1][1] < a[minIdx][minIdx]) minIdx = 1;
    if (a[2][2] < a[minIdx][minIdx]) minIdx = 2;

    UT_Vector3 normal((fpreal)v[0][minIdx],
                      (fpreal)v[1][minIdx],
                      (fpreal)v[2][minIdx]);

    fpreal len = normal.length();
    if (len < 1e-10) return UT_Vector3(0, 1, 0);
    normal /= len;

    // Orient outward: the normal should point away from the centroid of
    // neighbours relative to the query particle.
    UT_Vector3 outDir = p0 - centroid;
    if (outDir.dot(normal) < 0.0)
        normal = -normal;

    return normal;
}

// ── Main solve ─────────────────────────────────────────────────────────────
void NormalsSolver::solve(AreniteGeometry& geo) {
    VoxelGrid& g = geo.grid;
    if (g.cells.size() == 0) return;

    // ── 1. Mark grid cells as occupied ──────────────────────────────────
    for (auto& c : g.cells)
        c.occupied = false;

    for (const auto& p : geo.particles) {
        if (p.isEroded) continue;
        int ix, iy, iz;
        if (g.worldToGrid(p.position, ix, iy, iz))
            g.cells[g.flatIndex(ix, iy, iz)].occupied = true;
    }

    // ── 2. Build spatial hash for neighbour queries ──────────────────────
    buildSpatialHash(geo);

    // ── 3. Identify surface particles (paper §5.1) ───────────────────────
    // A particle is on the surface if its grid cell has at least one empty
    // (unoccupied) neighbour cell — the grid-occupancy criterion from the
    // paper.
    for (exint i = 0; i < geo.particles.entries(); ++i) {
        auto& p = geo.particles[i];
        if (p.isEroded) {
            p.isSurface = false;
            continue;
        }

        int ix, iy, iz;
        if (!g.worldToGrid(p.position, ix, iy, iz)) {
            p.isSurface = false;
            continue;
        }

        bool hasEmptyNeighbour = false;
        for (int dz = -1; dz <= 1 && !hasEmptyNeighbour; ++dz) {
            for (int dy = -1; dy <= 1 && !hasEmptyNeighbour; ++dy) {
                for (int dx = -1; dx <= 1 && !hasEmptyNeighbour; ++dx) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    int nx = ix + dx, ny = iy + dy, nz = iz + dz;
                    if (!g.inBounds(nx, ny, nz)) {
                        // Out-of-bounds counts as empty.
                        hasEmptyNeighbour = true;
                    } else if (!g.cells[g.flatIndex(nx, ny, nz)].occupied) {
                        hasEmptyNeighbour = true;
                    }
                }
            }
        }
        p.isSurface = hasEmptyNeighbour;
    }

    // ── 4. Estimate normals via PCA on k-nearest neighbours (paper §5.1) ─
    // Search radius = smoothingRadiusMult * dx, large enough to reliably
    // collect kNeighbours particles.
    fpreal searchRadius = smoothingRadiusMult * g.dx;

    for (exint i = 0; i < geo.particles.entries(); ++i) {
        auto& p = geo.particles[i];
        if (!p.isSurface || p.isEroded) continue;

        std::vector<exint> nbrs;
        kNearestNeighbours(geo, i, searchRadius, nbrs);
        p.normal = estimateNormalPCA(geo, i, nbrs);
    }
}
