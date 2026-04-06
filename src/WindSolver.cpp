#include "WindSolver.h"
#include <SYS/SYS_Math.h>

void WindSolver::solve(AreniteGeometry& geo, fpreal dt) {
    computeDeflation(geo);
    // Abrasion intentionally left at 0 for this subtask.
    // computeAbrasion(geo, dt);
}

void WindSolver::computeDeflation(AreniteGeometry& geo) {
    // Paper Eq. 12:
    //   W_d = k_d * exp( -(μ_c + μ_f * tr(σ))² / (2 α²) )
    //
    // Only surface particles are affected.  The deflation value is
    // accumulated into particle.erosionValue, which the ErosionSolver
    // will later consume via Eq. 1.
    //
    // Guard against α = 0 (would cause division by zero).
    if (windAlpha <= 0) return;

    fpreal twoAlphaSq = 2.0 * windAlpha * windAlpha;

    for (auto& p : geo.particles) {
        if (p.isEroded || !p.isSurface) continue;

        // Trace of the Cauchy stress tensor  tr(σ) = σ_xx + σ_yy + σ_zz
        fpreal trSigma = p.stressTensor(0, 0)
                        + p.stressTensor(1, 1)
                        + p.stressTensor(2, 2);

        // Critical normal force (Eq. 9):  F_c_n = μ_c + μ_f * tr(σ)
        fpreal Fcn = cohesion + frictionCoeff * trSigma;

        // Deflation (Eq. 12):  W_d = k_d * exp(-F_c_n² / (2α²))
        fpreal Wd = deflationCoeff * SYSexp(-(Fcn * Fcn) / twoAlphaSq);

        p.erosionValue += Wd;
    }
}

void WindSolver::computeAbrasion(AreniteGeometry& /*geo*/, fpreal /*dt*/) {
    // TODO: Maintain a set of wind-carried particles, simulate one DFSPH step
    //       to advect them, then accumulate abrasion onto sandstone surface
    //       particles based on collision speed and impact angle (Eq. 8).
}
