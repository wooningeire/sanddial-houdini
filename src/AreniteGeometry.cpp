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
    GA_ROHandleI sedH(geo->findPointAttribute("isSediment"));
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
        if (sedH.isValid())  p.isSediment  = (sedH.get(ptoff) != 0);

        // Reconstruct isEroded from viability: a particle is in-flight
        // (eroded) when its viability has been driven to zero.
        p.isEroded = (p.viability <= 0.0);

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

    if (useGroundPlane) {
        bmin.y() = SYSmax(bmin.y(), groundY);
    }

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
        p.deflationErosion = 0.0;
        p.abrasionErosion = 0.0;
        p.waterErosion = 0.0;
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
    GA_RWHandleI  sedH(geo->addIntTuple(GA_ATTRIB_POINT, "isSediment", 1));
    GA_RWHandleF  stressH(geo->addFloatTuple(GA_ATTRIB_POINT, "stress", 1));
    GA_RWHandleV3 normH(geo->addFloatTuple(GA_ATTRIB_POINT, "N", 3));
    GA_RWHandleF  deflH(geo->addFloatTuple(GA_ATTRIB_POINT, "wind_deflation", 1));
    GA_RWHandleF  abraH(geo->addFloatTuple(GA_ATTRIB_POINT, "wind_abrasion", 1));
    GA_RWHandleF  watrH(geo->addFloatTuple(GA_ATTRIB_POINT, "water_erosion", 1));
    GA_RWHandleF  totalH(geo->addFloatTuple(GA_ATTRIB_POINT, "total_erosion", 1));
    GA_RWHandleF  defGradH(geo->addFloatTuple(GA_ATTRIB_POINT, "deformationGrad", 9));
    GA_RWHandleF  apicCH(geo->addFloatTuple(GA_ATTRIB_POINT, "apicC", 9));

    for (const auto& p : particles) {
        // Write all non-eroded particles, plus eroded ones that are in-flight
        // (isEroded but not yet deposited).  Skipping eroded particles causes
        // them to vanish from the output and reappear at their original
        // position when deposited, which looks like teleportation.
        // We distinguish "truly gone" (should never happen in our model — we
        // reuse particles) from "in-flight eroded" by always writing them.
        // The isSediment and viability attributes let downstream code tell
        // them apart.

        GA_Offset pt = geo->appendPoint();
        geo->setPos3(pt, p.position);

        if (velH.isValid())  velH.set(pt, p.velocity);
        if (erodH.isValid()) erodH.set(pt, p.erodibility);
        if (viabH.isValid()) viabH.set(pt, p.viability);
        if (sedH.isValid())  sedH.set(pt, p.isSediment ? 1 : 0);
        if (normH.isValid()) normH.set(pt, p.normal);
        if (deflH.isValid()) deflH.set(pt, p.deflationErosion);
        if (abraH.isValid()) abraH.set(pt, p.abrasionErosion);
        if (watrH.isValid()) watrH.set(pt, p.waterErosion);
        if (totalH.isValid()) totalH.set(pt, p.erosionValue);

        if (stressH.isValid()) {
            fpreal sumSq = 0;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    sumSq += p.stressTensor(i, j) * p.stressTensor(i, j);
            stressH.set(pt, SYSsqrt(sumSq));
        }

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

void AreniteGeometry::writeGridToHoudiniGeo(GU_Detail* geo, int gridVisMode) const {
    if (!geo) return;
    geo->clearAndDestroy();

    GA_RWHandleV3 cdH(geo->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
    GA_RWHandleF  massH(geo->addFloatTuple(GA_ATTRIB_POINT, "mass", 1));
    GA_RWHandleV3 velH(geo->addFloatTuple(GA_ATTRIB_POINT, "v", 3));
    GA_RWHandleV3 forceH(geo->addFloatTuple(GA_ATTRIB_POINT, "force", 3));
    GA_RWHandleI  occH(geo->addIntTuple(GA_ATTRIB_POINT, "occupied", 1));

    for (int iz = 0; iz < grid.res[2]; ++iz) {
        for (int iy = 0; iy < grid.res[1]; ++iy) {
            for (int ix = 0; ix < grid.res[0]; ++ix) {
                const VoxelCell& cell = grid.cells[grid.flatIndex(ix, iy, iz)];
                
                // Only visualize occupied cells or cells with mass/force to avoid clutter.
                if (!cell.occupied && cell.mass < 1e-6 && cell.force.length2() < 1e-6)
                    continue;

                UT_Vector3 pos = grid.origin + UT_Vector3(ix + 0.5, iy + 0.5, iz + 0.5) * grid.dx;
                GA_Offset pt = geo->appendPoint();
                geo->setPos3(pt, pos);

                if (massH.isValid()) massH.set(pt, cell.mass);
                if (velH.isValid())  velH.set(pt, cell.velocity);
                if (forceH.isValid()) forceH.set(pt, cell.force);
                if (occH.isValid())  occH.set(pt, cell.occupied ? 1 : 0);

                if (cdH.isValid()) {
                    UT_Vector3 color(0.2, 0.2, 0.2); // Default gray
                    if (gridVisMode == 0) { // Mass
                        fpreal m = SYSclamp(cell.mass * 10.0, 0.0, 1.0);
                        color = UT_Vector3(m, m, m);
                    } else if (gridVisMode == 1) { // Momentum
                        fpreal len = cell.momentum.length();
                        if (len > 1e-5) {
                            color = (cell.momentum / len + UT_Vector3(1, 1, 1)) * 0.5;
                            color *= SYSclamp(len * 0.2, 0.2, 1.0);
                        }
                    } else if (gridVisMode == 2) { // Velocity (Direction)
                        fpreal len = cell.velocity.length();
                        if (len > 1e-5) {
                            color = (cell.velocity / len + UT_Vector3(1, 1, 1)) * 0.5;
                        }
                    } else if (gridVisMode == 3) { // Speed
                        fpreal s = SYSclamp(cell.velocity.length() * 0.5, 0.0, 1.0);
                        color = UT_Vector3(s, s * 0.5, 1.0 - s);
                    } else if (gridVisMode == 4) { // Force
                        fpreal f = SYSclamp(cell.force.length() * 0.1, 0.0, 1.0);
                        color = UT_Vector3(f, 0.5 * (1.0 - f), 1.0 - f);
                    } else if (gridVisMode == 5) { // Occupancy
                        color = cell.occupied ? UT_Vector3(0.2, 0.8, 0.2) : UT_Vector3(0.5, 0.1, 0.1);
                    }
                    cdH.set(pt, color);
                }
            }
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
