#include "ErosionSolver.h"
#include <SYS/SYS_Math.h>

void ErosionSolver::solve(AreniteGeometry& geo, fpreal dt) {
    // Paper Eq. 1:  ∂b/∂t = -E(σ_c) * (W + F)
    //
    // With wind abrasion W_a = 0 and fluvial erosion F = 0, this reduces to:
    //   ∂b/∂t = -E(σ_c) * W_d
    //
    // p.erosionValue already contains W_d (accumulated by the WindSolver).
    for (auto& p : geo.particles) {
        if (p.isEroded)
            continue;

        fpreal erod = computeErodibility(p);
        p.viability -= erod * p.erosionValue * dt;

        if (p.viability <= 0.0) {
            p.viability = 0.0;
            p.isEroded = true;
        }
    }
}

fpreal ErosionSolver::computeErodibility(const AreniteParticle& p) const {
    // Paper Eq. 2 (fabric interlocking):
    //   E(σ_c) = k_s   if tr(σ_c) > I
    //            k_w   otherwise
    //
    // where k_w >> k_s:  weak material erodes much faster than strong
    // (fabric-interlocked) material.
    //
    // tr(σ_c) = trace of the Cauchy stress tensor
    fpreal trSigma = p.stressTensor(0, 0)
                    + p.stressTensor(1, 1)
                    + p.stressTensor(2, 2);

    fpreal E = (trSigma > stressThreshold) ? strongErodibility
                                            : weakErodibility;

    // Multiply by the user-painted per-particle erodibility [0, 1]
    // so the artist retains direct control over which areas erode.
    return E * p.erodibility;
}
