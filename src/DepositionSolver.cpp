#include "DepositionSolver.h"
#include <cmath>
#include <random>
#include <algorithm>

void DepositionSolver::solve(AreniteGeometry& geo, fpreal dt, int frame) {
    UT_Vector3 noOffset(0, 0, 0);
    identifyStableCells(geo, noOffset);
    buildRoutingGraph(geo);
    depositParticles(geo, frame, noOffset);
}

void DepositionSolver::identifyStableCells(const AreniteGeometry& geo, const UT_Vector3& offset) {
    m_cellData.clear();
    m_cellData.resize(geo.grid.cells.entries());

    // Aggregate normals and Y positions for surface particles
    for (const auto& p : geo.particles) {
        if (!p.isSurface || p.isEroded) continue;
        
        int cx, cy, cz;
        if (geo.grid.worldToGrid(p.position + offset, cx, cy, cz)) {
            exint idx = geo.grid.flatIndex(cx, cy, cz);
            if (idx >= 0 && idx < m_cellData.size()) {
                m_cellData[idx].surfaceCount++;
                m_cellData[idx].sumNormal += p.normal;
                m_cellData[idx].sumY += p.position.y();
            }
        }
    }

    // Compute average slope and determine stability
    for (size_t i = 0; i < m_cellData.size(); ++i) {
        auto& cell = m_cellData[i];
        if (cell.surfaceCount > 0) {
            cell.proxyElevation = cell.sumY / cell.surfaceCount;
            UT_Vector3 avgNormal = cell.sumNormal;
            avgNormal.normalize();
            
            // Slope is angle between normal and (0,1,0)
            fpreal ny = static_cast<fpreal>(avgNormal.y());
            cell.averageSlope = std::acos(std::max(static_cast<fpreal>(-1.0), std::min(static_cast<fpreal>(1.0), ny)));
            cell.isStable = (cell.averageSlope < stableSlopeThreshold);
        } else {
            cell.isStable = false;
            int iz = i / (geo.grid.res[0] * geo.grid.res[1]);
            int rem = i % (geo.grid.res[0] * geo.grid.res[1]);
            int iy = rem / geo.grid.res[0];
            cell.proxyElevation = geo.grid.origin.y() + iy * geo.grid.dx + geo.grid.dx * 0.5f;
        }
    }
}

void DepositionSolver::buildRoutingGraph(const AreniteGeometry& geo) {
    const int rx = geo.grid.res[0];
    const int ry = geo.grid.res[1];
    const int rz = geo.grid.res[2];

    // Find receivers
    for (int iz = 0; iz < rz; ++iz) {
        for (int iy = 0; iy < ry; ++iy) {
            for (int ix = 0; ix < rx; ++ix) {
                exint c_idx = geo.grid.flatIndex(ix, iy, iz);
                auto& c_cell = m_cellData[c_idx];

                if (c_cell.surfaceCount == 0) {
                    // Empty cell: gravity pulls straight down
                    if (iy > 0) {
                        c_cell.receiverIdx = geo.grid.flatIndex(ix, iy - 1, iz);
                    } else {
                        c_cell.receiverIdx = c_idx;
                    }
                } else if (c_cell.isStable) {
                    c_cell.receiverIdx = c_idx;
                } else {
                    int maxIdx = c_idx;
                    fpreal maxSlope = 0.0;

                    // Check 26 neighbors
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0 && dz == 0) continue;
                                int nx = ix + dx;
                                int ny = iy + dy;
                                int nz = iz + dz;
                                if (geo.grid.inBounds(nx, ny, nz)) {
                                    exint n_idx = geo.grid.flatIndex(nx, ny, nz);
                                    const auto& n_cell = m_cellData[n_idx];
                                    
                                    if (n_cell.proxyElevation < c_cell.proxyElevation) {
                                        fpreal drop = c_cell.proxyElevation - n_cell.proxyElevation;
                                        fpreal dist = std::sqrt((fpreal)(dx*dx + dy*dy + dz*dz)) * geo.grid.dx;
                                        fpreal slope = drop / dist;
                                        if (slope > maxSlope) {
                                            maxSlope = slope;
                                            maxIdx = n_idx;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    c_cell.receiverIdx = maxIdx;
                }
                c_cell.pIdx = c_cell.receiverIdx;
            }
        }
    }

    // Pointer jumping
    int N = rx * ry * rz;
    int numIterations = 0;
    if (N > 0) numIterations = static_cast<int>(std::ceil(std::log2(N)));

    for (int iter = 0; iter < numIterations; ++iter) {
        std::vector<int> nextP(N, -1);
        for (int i = 0; i < N; ++i) {
            int p = m_cellData[i].pIdx;
            if (p >= 0) {
                nextP[i] = m_cellData[p].pIdx;
            } else {
                nextP[i] = p;
            }
        }
        for (int i = 0; i < N; ++i) {
            m_cellData[i].pIdx = nextP[i];
        }
    }
}

void DepositionSolver::depositParticles(AreniteGeometry& geo, int frame, const UT_Vector3& offset) {
    std::mt19937 rng(frame * 67891);
    // Wider jitter: particles can bleed half a cell beyond their destination
    // cell in each direction, eliminating visible cell-edge clustering.
    std::uniform_real_distribution<fpreal> xzDist(-0.5 * geo.grid.dx,
                                                    1.5 * geo.grid.dx);
    // Vertical perturbation so deposited layers aren't flat shelves.
    std::uniform_real_distribution<fpreal> yDist(-0.5 * geo.grid.dx,
                                                   0.5 * geo.grid.dx);

    for (auto& p : geo.particles) {
        if (!p.isEroded) continue;

        int cx, cy, cz;
        if (geo.grid.worldToGrid(p.position + offset, cx, cy, cz)) {
            exint idx = geo.grid.flatIndex(cx, cy, cz);
            if (idx >= 0 && idx < m_cellData.size()) {
                int destIdx = m_cellData[idx].pIdx;
                if (destIdx >= 0 && destIdx < m_cellData.size()) {
                    if (destIdx == idx) {
                        p.isEroded = false;
                        p.viability = 1.0f;
                        continue;
                    }

                    auto& d_cell = m_cellData[destIdx];
                    
                    int dz = destIdx / (geo.grid.res[0] * geo.grid.res[1]);
                    int rem = destIdx % (geo.grid.res[0] * geo.grid.res[1]);
                    int dy = rem / geo.grid.res[0];
                    int dx = rem % geo.grid.res[0];

                    fpreal rx = xzDist(rng);
                    fpreal rz = xzDist(rng);
                    fpreal ry = yDist(rng);
                    
                    p.position.x() = geo.grid.origin.x() + dx * geo.grid.dx + rx;
                    p.position.z() = geo.grid.origin.z() + dz * geo.grid.dx + rz;
                    p.position.y() = d_cell.proxyElevation + ry;
                    
                    // Increment proxy elevation to maintain density and create a pile.
                    d_cell.proxyElevation += geo.grid.dx * 0.005f;

                    p.isEroded = false;
                    p.viability = 1.0f;
                }
            }
        }
    }
}
