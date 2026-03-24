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

void DepositionSolver::depositParticles(AreniteGeometry& /*geo*/) {
    // TODO: For each eroded particle, follow the routing graph to a stable
    //       cell and place the particle at the surface of that cell.
    //       Clear the particle's eroded flag and reset its viability.
}
