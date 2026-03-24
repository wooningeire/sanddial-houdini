#include "AreniteGeometry.h"

void AreniteGeometry::initFromPositions(const UT_Array<UT_Vector3>& positions) {
    particles.setSize(positions.size());
    for (exint i = 0; i < positions.size(); ++i) {
        particles[i] = AreniteParticle();
        particles[i].position = positions[i];
    }
}

void AreniteGeometry::resetStepData() {
    for (auto& p : particles) {
        p.erosionValue = 0.0;
        p.isSurface = false;
    }
}

int AreniteGeometry::aliveCount() const {
    int count = 0;
    for (const auto& p : particles) {
        if (!p.isEroded)
            ++count;
    }
    return count;
}
