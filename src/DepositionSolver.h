#pragma once

#include "AreniteGeometry.h"

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

    /// Process all eroded particles: route them to stable cells and deposit.
    void solve(AreniteGeometry& geo, fpreal dt);

private:
    /// Identify which grid cells are "stable" based on average slope.
    void identifyStableCells(const AreniteGeometry& geo);

    /// Build a routing graph from each cell to the nearest reachable stable
    /// cell.
    void buildRoutingGraph(const AreniteGeometry& geo);

    /// Move each eroded particle along the routing graph and deposit it.
    void depositParticles(AreniteGeometry& geo);
};
