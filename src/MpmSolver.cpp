#include "MpmSolver.h"

void MpmSolver::solve(AreniteGeometry& geo, fpreal dt) {
    transferToGrid(geo);
    computeGridForces(dt);
    transferToParticles(geo, dt);
}

void MpmSolver::transferToGrid(const AreniteGeometry& /*geo*/) {
    // TODO: Scatter particle mass and momentum onto the background MPM grid
    //       using quadratic B-spline weights (MLS-MPM).
}

void MpmSolver::computeGridForces(fpreal /*dt*/) {
    // TODO: Compute elastic forces from the deformation gradient and apply
    //       gravity on the grid.  Update grid velocities.
}

void MpmSolver::transferToParticles(AreniteGeometry& /*geo*/, fpreal /*dt*/) {
    // TODO: Gather updated velocities from the grid back to particles (G2P).
    //       Update deformation gradients and compute the Cauchy stress tensor.
}
