#include "WindSolver.h"

void WindSolver::solve(AreniteGeometry& geo, fpreal dt) {
    computeDeflation(geo);
    computeAbrasion(geo, dt);
}

void WindSolver::computeDeflation(AreniteGeometry& /*geo*/) {
    // TODO: For each surface particle, estimate the probability of
    //       dislodgement by wind friction using the formula from the Arenite
    //       paper (Eq. relating shear velocity to particle threshold).
    //       Accumulate the deflation erosion into particle.erosionValue.
}

void WindSolver::computeAbrasion(AreniteGeometry& /*geo*/, fpreal /*dt*/) {
    // TODO: Maintain a set of wind-carried particles, simulate one DFSPH step
    //       to advect them, then accumulate abrasion onto sandstone surface
    //       particles based on collision speed and impact angle.
}
