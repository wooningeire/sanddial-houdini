#include "AreniteGeometry.h"
#include <GU/GU_Detail.h>
#include <GA/GA_Handle.h>
#include <GA/GA_Iterator.h>
#include <SYS/SYS_Math.h>

// ── VoxelGrid ──────────────────────────────────────────────────────────────

void VoxelGrid::allocate(int rx, int ry, int rz, fpreal cellSize,
                         const UT_Vector3& org) {
    res[0] = rx;
    res[1] = ry;
    res[2] = rz;
    dx     = cellSize;
    origin = org;

    exint total = (exint)rx * (exint)ry * (exint)rz;
    cells.setSize(total);
    clear();
}

void VoxelGrid::clear() {
    for (auto& c : cells) {
        c.mass     = 0.0;
        c.momentum = UT_Vector3(0, 0, 0);
        c.velocity = UT_Vector3(0, 0, 0);
        c.force    = UT_Vector3(0, 0, 0);
        c.occupied = false;
    }
}

bool VoxelGrid::worldToGrid(const UT_Vector3& pos,
                            int& ix, int& iy, int& iz) const {
    UT_Vector3 local = (pos - origin) / dx;
    ix = (int)SYSfloor(local.x());
    iy = (int)SYSfloor(local.y());
    iz = (int)SYSfloor(local.z());
    return inBounds(ix, iy, iz);
}

// ── AreniteGeometry ────────────────────────────────────────────────────────

void AreniteGeometry::initFromPositions(const UT_Array<UT_Vector3>& positions) {
    particles.setSize(positions.size());
    for (exint i = 0; i < positions.size(); ++i) {
        particles[i] = AreniteParticle();
        particles[i].position = positions[i];
    }
}

void AreniteGeometry::initFromHoudiniGeo(const GU_Detail* geo) {
    if (!geo) return;

    exint npts = geo->getNumPoints();
    particles.setSize(npts);

    GA_ROHandleF erodH(geo->findPointAttribute("erodibility"));

    exint idx = 0;
    GA_Offset ptoff;
    GA_FOR_ALL_PTOFF(geo, ptoff) {
        AreniteParticle& p = particles[idx];
        p = AreniteParticle();
        p.position  = geo->getPos3(ptoff);
        p.viability = 1.0;
        p.erodibility = erodH.isValid() ? erodH.get(ptoff) : 1.0;
        ++idx;
    }
}

void AreniteGeometry::initGrid() {
    if (particles.size() == 0) return;

    // Compute bounding box of all particles.
    UT_Vector3 bmin = particles[0].position;
    UT_Vector3 bmax = particles[0].position;
    for (const auto& p : particles) {
        bmin.x() = SYSmin(bmin.x(), p.position.x());
        bmin.y() = SYSmin(bmin.y(), p.position.y());
        bmin.z() = SYSmin(bmin.z(), p.position.z());
        bmax.x() = SYSmax(bmax.x(), p.position.x());
        bmax.y() = SYSmax(bmax.y(), p.position.y());
        bmax.z() = SYSmax(bmax.z(), p.position.z());
    }

    // Add padding.
    bmin -= domainPadding;
    bmax += domainPadding;

    // Compute grid resolution from voxel size.
    fpreal dx = voxelSize;
    if (dx < 1e-6) dx = 0.1;

    int rx = SYSmax(1, (int)SYSceil((bmax.x() - bmin.x()) / dx));
    int ry = SYSmax(1, (int)SYSceil((bmax.y() - bmin.y()) / dx));
    int rz = SYSmax(1, (int)SYSceil((bmax.z() - bmin.z()) / dx));

    grid.allocate(rx, ry, rz, dx, bmin);
}

void AreniteGeometry::resetStepData() {
    for (auto& p : particles) {
        p.erosionValue = 0.0;
        p.isSurface = false;
    }
    grid.clear();
}

void AreniteGeometry::writeToHoudiniGeo(GU_Detail* geo) const {
    if (!geo) return;

    geo->clearAndDestroy();

    // Create point attributes.
    GA_RWHandleV3 velH(geo->addFloatTuple(GA_ATTRIB_POINT, "v", 3));
    GA_RWHandleF  erodH(geo->addFloatTuple(GA_ATTRIB_POINT, "erodibility", 1));
    GA_RWHandleF  viabH(geo->addFloatTuple(GA_ATTRIB_POINT, "viability", 1));

    for (const auto& p : particles) {
        if (p.isEroded)
            continue;

        GA_Offset pt = geo->appendPoint();
        geo->setPos3(pt, p.position);

        if (velH.isValid())  velH.set(pt, p.velocity);
        if (erodH.isValid()) erodH.set(pt, p.erodibility);
        if (viabH.isValid()) viabH.set(pt, p.viability);
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
