#include "WaterSolver.h"

void WaterSolver::solve(AreniteGeometry& geo, fpreal dt) {
    buildFlowGraph(geo);
    computeDischarge();
    accumulateErosion(geo);
}

void WaterSolver::buildFlowGraph(const AreniteGeometry& /*geo*/) {
    // TODO: For each surface cell, compute the slope (from particle normals)
    //       and determine "receiver" cells that water flows toward using the
    //       3D FastFlow generalisation described in Arenite.
}

void WaterSolver::computeDischarge() {
    // TODO: Route precipitation through the flow graph to compute the
    //       cumulative fluvial discharge at each cell.  Handle depressions
    //       (local minima) by flooding/filling as in FastFlow.
}

void WaterSolver::accumulateErosion(AreniteGeometry& /*geo*/) {
    // TODO: Compute fluvial erosion per particle from discharge, slope, and
    //       critical shear stress.  Accumulate into particle.erosionValue.
}
