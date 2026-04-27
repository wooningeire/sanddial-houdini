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

    // Load grid configuration from detail attributes if present
    GA_ROHandleI resH(geo->findAttribute(GA_ATTRIB_DETAIL, "grid_res"));
    GA_ROHandleF dxH(geo->findAttribute(GA_ATTRIB_DETAIL, "grid_dx"));
    GA_ROHandleV3 orgH(geo->findAttribute(GA_ATTRIB_DETAIL, "grid_origin"));

    if (resH.isValid() && dxH.isValid() && orgH.isValid()) {
        int rx = resH.get(GA_Offset(0), 0);
        int ry = resH.get(GA_Offset(0), 1);
        int rz = resH.get(GA_Offset(0), 2);
        grid.allocate(rx, ry, rz, dxH.get(GA_Offset(0)), orgH.get(GA_Offset(0)));
    }

    exint npts = geo->getNumPoints();
    particles.setSize(npts);

    GA_ROHandleF erodH(geo->findPointAttribute("erodibility"));
    GA_ROHandleV3 velH(geo->findPointAttribute("v"));
    GA_ROHandleF viabH(geo->findPointAttribute("viability"));
    GA_ROHandleF defGradH(geo->findPointAttribute("deformationGrad"));
    GA_ROHandleF apicCH(geo->findPointAttribute("apicC"));

    exint idx = 0;
    GA_Offset ptoff;
    GA_FOR_ALL_PTOFF(geo, ptoff) {
        AreniteParticle& p = particles[idx];
        p = AreniteParticle();
        p.position  = geo->getPos3(ptoff);
        
        if (erodH.isValid()) p.erodibility = erodH.get(ptoff);
        if (velH.isValid())  p.velocity    = velH.get(ptoff);
        if (viabH.isValid()) p.viability   = viabH.get(ptoff);

        if (defGradH.isValid() && defGradH.getTupleSize() == 9) {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    p.deformationGrad(i, j) = defGradH.get(ptoff, i * 3 + j);
        }

        if (apicCH.isValid() && apicCH.getTupleSize() == 9) {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    p.apicC(i, j) = apicCH.get(ptoff, i * 3 + j);
        }

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

    // Save grid configuration as detail attributes
    GA_RWHandleI resH(geo->addIntTuple(GA_ATTRIB_DETAIL, "grid_res", 3));
    GA_RWHandleF dxH(geo->addFloatTuple(GA_ATTRIB_DETAIL, "grid_dx", 1));
    GA_RWHandleV3 orgH(geo->addFloatTuple(GA_ATTRIB_DETAIL, "grid_origin", 3));

    if (resH.isValid()) {
        resH.set(GA_Offset(0), 0, grid.res[0]);
        resH.set(GA_Offset(0), 1, grid.res[1]);
        resH.set(GA_Offset(0), 2, grid.res[2]);
    }
    if (dxH.isValid()) dxH.set(GA_Offset(0), grid.dx);
    if (orgH.isValid()) orgH.set(GA_Offset(0), grid.origin);

    // Create point attributes.
    GA_RWHandleV3 velH(geo->addFloatTuple(GA_ATTRIB_POINT, "v", 3));
    GA_RWHandleF  erodH(geo->addFloatTuple(GA_ATTRIB_POINT, "erodibility", 1));
    GA_RWHandleF  viabH(geo->addFloatTuple(GA_ATTRIB_POINT, "viability", 1));
    GA_RWHandleF  defGradH(geo->addFloatTuple(GA_ATTRIB_POINT, "deformationGrad", 9));
    GA_RWHandleF  apicCH(geo->addFloatTuple(GA_ATTRIB_POINT, "apicC", 9));

    for (const auto& p : particles) {
        if (p.isEroded)
            continue;

        GA_Offset pt = geo->appendPoint();
        geo->setPos3(pt, p.position);

        if (velH.isValid())  velH.set(pt, p.velocity);
        if (erodH.isValid()) erodH.set(pt, p.erodibility);
        if (viabH.isValid()) viabH.set(pt, p.viability);

        if (defGradH.isValid()) {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    defGradH.set(pt, i * 3 + j, p.deformationGrad(i, j));
        }

        if (apicCH.isValid()) {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    apicCH.set(pt, i * 3 + j, p.apicC(i, j));
        }
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
