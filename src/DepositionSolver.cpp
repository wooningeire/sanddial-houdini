#include "DepositionSolver.h"

void DepositionSolver::solve(AreniteGeometry& geo, fpreal dt) {
    identifyStableCells(geo);
    buildRoutingGraph(geo);
    depositParticles(geo);
}

void DepositionSolver::identifyStableCells(const AreniteGeometry& /*geo*/) {
    // TODO: Iterate over occupied grid cells and compute the average slope
    //       from particle normals.  Mark cells with slope < stableSlopeThreshold
    //       as stable.
}

void DepositionSolver::buildRoutingGraph(const AreniteGeometry& /*geo*/) {
    // TODO: BFS / shortest-path from every non-stable cell to the nearest
    //       stable cell, recording the route for particle transport.
}

void DepositionSolver::depositParticles(AreniteGeometry& geo) {
    // Placeholder Alpha implementation: teleport eroded particles to y = 0
    for (auto& p : geo.particles) {
        if (p.isEroded) {
            p.position.y() = 0.0f;
            p.isEroded = false;
            p.viability = 1.0f;
        }
    }
}
