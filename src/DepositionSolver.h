#pragma once

#include "AreniteGeometry.h"
#include <vector>

struct DepCell {
    int surfaceCount = 0;
    UT_Vector3 sumNormal{0, 0, 0};
    fpreal sumY = 0.0;
    
    fpreal proxyElevation = 0.0;
    fpreal averageSlope = 0.0;
    
    bool isStable = false;
    int receiverIdx = -1;
    int pIdx = -1;
};

/// Routes eroded particles toward stable cells and deposits them.
///
/// "Stable" cells are those whose average surface slope is below a threshold.
/// A graph connecting each cell to a reachable stable cell is built, and
/// eroded particles are transported along it to their deposition site.
class DepositionSolver {
public:
    DepositionSolver() = default;
    ~DepositionSolver() = default;

    // ── Parameters ──────────────────────────────────────────────────────────
    /// Maximum slope (in radians) below which a cell is considered stable.
    fpreal stableSlopeThreshold = 0.5;

    /// Initial viability assigned to deposited sediment.
    fpreal sedimentViability = 0.05;

    /// Process all eroded particles: route them to stable cells and deposit.
    void solve(AreniteGeometry& geo, fpreal dt, int frame);

private:
    /// Identify which grid cells are "stable" based on average slope.
    void identifyStableCells(const AreniteGeometry& geo, const UT_Vector3& offset);

    /// Build a routing graph from each cell to the nearest reachable stable
    /// cell.
    void buildRoutingGraph(const AreniteGeometry& geo);

    /// Move each eroded particle along the routing graph and deposit it.
    void depositParticles(AreniteGeometry& geo, int frame, const UT_Vector3& offset);

    std::vector<DepCell> m_cellData;
};
