#include "ErosionSolver.h"
#include <SYS/SYS_Math.h>

void ErosionSolver::solve(AreniteGeometry& geo, fpreal dt) {
    for (auto& p : geo.particles) {
        if (p.isEroded)
            continue;

        fpreal erod = computeErodibility(p);
        p.viability -= erod * p.erosionValue * dt;

        if (p.viability <= 0.0) {
            p.isEroded = true;
        }
    }
}

fpreal ErosionSolver::computeErodibility(const AreniteParticle& p) const {
    // TODO: Compute stress magnitude from the Cauchy stress tensor and
    //       interpolate between weakErodibility and strongErodibility based
    //       on stressThreshold.  Also factor in the user-painted erodibility.
    //       For now, just return the particle's painted erodibility directly.
    return p.erodibility;
}
