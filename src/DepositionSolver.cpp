#include "DepositionSolver.h"
#include <cmath>
#include <random>
#include <algorithm>
#include <limits>

// ── Top-level ──────────────────────────────────────────────────────────────
void DepositionSolver::solve(AreniteGeometry& geo, fpreal dt, int frame) {
    // Paper §7: "we move the grid by a random offset for particles at each
    // deposition step, ensuring particles are not grouped in the same grid
    // cell each iteration."
    std::mt19937 offsetRng(frame * 1234567u);
    std::uniform_real_distribution<fpreal> offsetDist(0.0f, geo.grid.dx);
    UT_Vector3 gridOffset(offsetDist(offsetRng),
                          0.0f,
                          offsetDist(offsetRng));

    identifySurfaceCells(geo, gridOffset);
    assignReceivers(geo);
    buildDepositionPointers();
    depositParticles(geo, frame, gridOffset);
}

// ── Step 1: identify surface cells and classify stable/unstable ────────────
// "We define a stable cell as a grid cell where the average slope of surface
//  particles is less than this critical angle." (paper §7)
void DepositionSolver::identifySurfaceCells(const AreniteGeometry& geo,
                                            const UT_Vector3& gridOffset) {
    const int N = geo.grid.res[0] * geo.grid.res[1] * geo.grid.res[2];
    m_cells.assign(N, DepCell{});

    // Accumulate surface-particle normals and Y positions per cell.
    // Use the shifted grid position so the random offset is applied.
    struct Accum { UT_Vector3 sumNormal{0,0,0}; fpreal sumY = 0; int count = 0; };
    std::vector<Accum> acc(N);

    for (const auto& p : geo.particles) {
        if (p.isEroded || !p.isSurface) continue;
        int ix, iy, iz;
        if (!geo.grid.worldToGrid(p.position + gridOffset, ix, iy, iz)) continue;
        exint ci = geo.grid.flatIndex(ix, iy, iz);
        acc[ci].sumNormal += p.normal;
        acc[ci].sumY      += p.position.y();
        acc[ci].count++;
    }

    for (int i = 0; i < N; ++i) {
        if (acc[i].count == 0) continue;
        auto& c = m_cells[i];
        c.isSurface      = true;
        c.proxyElevation = acc[i].sumY / acc[i].count;
        c.depositTop     = c.proxyElevation;

        UT_Vector3 avgN = acc[i].sumNormal;
        avgN.normalize();
        fpreal ny = std::max(-1.0f, std::min(1.0f, (float)avgN.y()));
        c.averageSlope = std::acos(ny);
        c.isStable     = (c.averageSlope < stableSlopeThreshold);
    }
}

// ── Step 2: assign receivers (paper §6.2 Fig. 4, three-case rule) ─────────
// This is the same receiver logic used for fluvial flow routing, reused here
// for deposition as stated in paper §7: "we reuse the 3D grid that embeds
// the receiver information".
void DepositionSolver::assignReceivers(const AreniteGeometry& geo) {
    const int rx = geo.grid.res[0];
    const int ry = geo.grid.res[1];
    const int rz = geo.grid.res[2];

    const int hDx[4] = {-1, 1,  0, 0};
    const int hDz[4] = { 0, 0, -1, 1};

    for (int iz = 0; iz < rz; ++iz) {
        for (int iy = 0; iy < ry; ++iy) {
            for (int ix = 0; ix < rx; ++ix) {
                int ci = (int)geo.grid.flatIndex(ix, iy, iz);
                m_cells[ci].receiverIdx = ci; // default: self (terminal)

                // Case (a): cell directly below is empty → receiver is below.
                if (iy > 0) {
                    int below = (int)geo.grid.flatIndex(ix, iy - 1, iz);
                    if (!geo.grid.cells[below].occupied) {
                        m_cells[ci].receiverIdx = below;
                        continue;
                    }
                }

                // Case (b): a horizontal neighbour cell is empty → receiver
                // is that empty neighbour (particle slides off the edge).
                {
                    int empties[4]; int ne = 0;
                    for (int d = 0; d < 4; ++d) {
                        int nx = ix + hDx[d], nz = iz + hDz[d];
                        if (!geo.grid.inBounds(nx, iy, nz)) continue;
                        int ni = (int)geo.grid.flatIndex(nx, iy, nz);
                        if (!geo.grid.cells[ni].occupied)
                            empties[ne++] = ni;
                    }
                    if (ne > 0) {
                        // Pick the empty neighbour with the lowest proxy
                        // elevation (or self-elevation for empty cells, which
                        // is just the grid row height — always lower than a
                        // surface cell above it).
                        int best = empties[0];
                        for (int k = 1; k < ne; ++k)
                            if (m_cells[empties[k]].proxyElevation <
                                m_cells[best].proxyElevation)
                                best = empties[k];
                        m_cells[ci].receiverIdx = best;
                        continue;
                    }
                }

                // Case (c): surface horizontal neighbour with strictly lower
                // proxy elevation → receiver is the lowest such neighbour.
                if (m_cells[ci].isSurface) {
                    int best = -1;
                    fpreal bestElev = m_cells[ci].proxyElevation;
                    for (int d = 0; d < 4; ++d) {
                        int nx = ix + hDx[d], nz = iz + hDz[d];
                        if (!geo.grid.inBounds(nx, iy, nz)) continue;
                        int ni = (int)geo.grid.flatIndex(nx, iy, nz);
                        if (m_cells[ni].isSurface &&
                            m_cells[ni].proxyElevation < bestElev) {
                            bestElev = m_cells[ni].proxyElevation;
                            best     = ni;
                        }
                    }
                    if (best >= 0) {
                        m_cells[ci].receiverIdx = best;
                        continue;
                    }
                }
                // No lower neighbour found: self-loop (local minimum / stable
                // base).  pIdx will be set to self in the next step.
            }
        }
    }
}

// ── Step 3: pointer jumping (paper §7) ────────────────────────────────────
// "Initially, p(c) is set to the receiver of cell c.  We iteratively update
//  these pointers using a parallel pointer jumping technique: p(c) = p(p(c)).
//  This process is repeated for a maximum of log(n) iterations, where n is
//  the total number of surface cells."
void DepositionSolver::buildDepositionPointers() {
    const int N = (int)m_cells.size();

    // Count surface cells to determine iteration count.
    int nSurface = 0;
    for (int i = 0; i < N; ++i)
        if (m_cells[i].isSurface) ++nSurface;

    // Initialise: p(c) = receiver(c).
    // Stable cells point to themselves (they are the deposition target).
    for (int i = 0; i < N; ++i) {
        auto& c = m_cells[i];
        c.pIdx = c.isStable ? i : c.receiverIdx;
    }

    if (nSurface < 2) return;
    int iters = (int)std::ceil(std::log2((double)nSurface));

    std::vector<int> next(N);
    for (int iter = 0; iter < iters; ++iter) {
        for (int i = 0; i < N; ++i) {
            int p = m_cells[i].pIdx;
            // Follow one more hop, but only if the target is a valid surface
            // cell and is not already stable (stable cells are terminals).
            if (p >= 0 && p < N && p != i && !m_cells[p].isStable)
                next[i] = m_cells[p].pIdx;
            else
                next[i] = p;
        }
        for (int i = 0; i < N; ++i)
            m_cells[i].pIdx = next[i];
    }

    // Clamp: any pointer that still doesn't land on a stable surface cell
    // falls back to self (deposit at source).
    for (int i = 0; i < N; ++i) {
        int p = m_cells[i].pIdx;
        if (p < 0 || p >= N || !m_cells[p].isSurface || !m_cells[p].isStable)
            m_cells[i].pIdx = i;
    }
}

// ── Step 4: deposit eroded particles ──────────────────────────────────────
// "Each cell containing eroded particles uses p to increase the count of the
//  number of particles that should be deposited in each stable cell.  We
//  deposit particles at random 2D positions within the cell and at the cell
//  proxy elevation z, increasing after each deposited particle to maintain a
//  target particle density." (paper §7)
void DepositionSolver::depositParticles(AreniteGeometry& geo, int frame,
                                        const UT_Vector3& gridOffset) {
    std::mt19937 rng(frame * 98765u);
    std::uniform_real_distribution<fpreal> cellRand(0.0f, 1.0f);

    const int rx = geo.grid.res[0];
    const int ry = geo.grid.res[1];
    const fpreal dx = geo.grid.dx;

    for (auto& p : geo.particles) {
        if (!p.isEroded) continue;

        // Find the surface cell this particle came from.
        // Use the same shifted grid so the lookup is consistent with step 1.
        int cx, cy, cz;
        if (!geo.grid.worldToGrid(p.position + gridOffset, cx, cy, cz)) {
            // Outside grid — deposit in place (no movement).
            p.isEroded   = false;
            p.isSediment = true;
            p.viability  = sedimentViability;
            continue;
        }

        int srcIdx = (int)geo.grid.flatIndex(cx, cy, cz);

        // Follow the deposition pointer to the stable target cell.
        int destIdx = m_cells[srcIdx].pIdx;
        if (destIdx < 0 || destIdx >= (int)m_cells.size())
            destIdx = srcIdx;

        auto& dest = m_cells[destIdx];

        // Decompose destIdx to grid coordinates.
        int diz = destIdx / (rx * ry);
        int rem = destIdx % (rx * ry);
        int dix = rem % rx;

        // Random 2D position within the destination cell (paper §7).
        // Subtract gridOffset so the world position is correct.
        p.position.x() = (geo.grid.origin.x() - gridOffset.x())
                         + dix * dx + cellRand(rng) * dx;
        p.position.z() = (geo.grid.origin.z() - gridOffset.z())
                         + diz * dx + cellRand(rng) * dx;

        // Place at the current pile top and increment it so subsequent
        // particles stack (paper §7: "increasing after each deposited
        // particle to maintain a target particle density").
        p.position.y() = dest.depositTop;
        dest.depositTop += dx * 0.5f;
        dest.depositCount++;

        p.isEroded   = false;
        p.isSediment = true;
        p.viability  = sedimentViability;
    }
}
