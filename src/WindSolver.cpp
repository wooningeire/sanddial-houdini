#include "WindSolver.h"
#include <SYS/SYS_Math.h>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void WindSolver::solve(AreniteGeometry& geo, fpreal dt) {
    computeDeflation(geo);
    computeAbrasion(geo, dt);
}

void WindSolver::computeDeflation(AreniteGeometry& geo) {
    if (windAlpha <= 0) return;

    fpreal twoAlphaSq = 2.0 * windAlpha * windAlpha;

    // Wind speed follows a logarithmic boundary layer profile near the ground.
    // We use a simple linear ramp over one smoothing length as an approximation:
    // occlusion = clamp((y - groundY) / boundaryLayerHeight, 0, 1)
    // Particles at or below groundY receive zero deflation.
    fpreal boundaryLayerHeight = smoothingLength > 0 ? smoothingLength : geo.grid.dx;

    for (auto& p : geo.particles) {
        if (p.isEroded || !p.isSurface) continue;

        // Ground-plane occlusion factor.
        fpreal groundOcclusion = 1.0;
        if (geo.useGroundPlane) {
            fpreal heightAboveGround = p.position.y() - geo.groundY;
            groundOcclusion = SYSclamp(heightAboveGround / boundaryLayerHeight, fpreal(0.0), fpreal(1.0));
        }
        if (groundOcclusion <= 0.0) continue;

        fpreal trSigma = p.stressTensor(0, 0)
                        + p.stressTensor(1, 1)
                        + p.stressTensor(2, 2);

        fpreal Fcn = cohesion + frictionCoeff * trSigma;
        fpreal Wd = deflationCoeff * SYSexp(-(Fcn * Fcn) / twoAlphaSq) * groundOcclusion;
        p.erosionValue += Wd;
        p.deflationErosion += Wd;
    }
}

void WindSolver::computeAbrasion(AreniteGeometry& geo, fpreal dt) {
    if (geo.grid.cells.isEmpty() || geo.grid.dx < 1e-6) return;

    // Wind domain: a box centred on the grid centre, expanded by
    // WIND_DOMAIN_EXPANSION on each axis.  Used by BOTH emission (spawns
    // particles on the upstream face of this box) AND the cleanup pass
    // below (removes anything that has exited it).  These two MUST use
    // identical bounds -- previously emission used grid_size * 2 while
    // cleanup used grid_bbox padded by 2*domainPadding, so for any grid
    // bigger than ~4*domainPadding the emission face was *outside* the
    // cleanup region and every spawned particle was deleted on the same
    // step, producing the "no abrasion particles spawn" symptom.
    UT_Vector3 size(geo.grid.res[0] * geo.grid.dx,
                    geo.grid.res[1] * geo.grid.dx,
                    geo.grid.res[2] * geo.grid.dx);
    UT_Vector3 center        = geo.grid.origin + size * 0.5;
    UT_Vector3 expandedSize  = size * WIND_DOMAIN_EXPANSION;
    UT_Vector3 windMin       = center - expandedSize * 0.5;
    UT_Vector3 windMax       = center + expandedSize * 0.5;

    // 1. Emit new wind particles at the upstream boundary.
    emitWindParticles(geo, dt, windMin, windMax);

    // 2. Update wind particle dynamics (SPH step).
    updateWindParticles(geo, dt);

    // 3. Accumulate abrasion on sandstone surface particles (Eq. 8).
    // Eq. 8: W_a = k_a * ||v|| * (-n . v)+

    // Optimization: Build a temporary map from voxel cells to sandstone particles.

    std::vector<std::vector<exint>> cellToParticles(geo.grid.cells.size());
    for (exint i = 0; i < geo.particles.size(); ++i) {
        const auto& p = geo.particles[i];
        if (p.isEroded || !p.isSurface) continue;
        int ix, iy, iz;
        if (geo.grid.worldToGrid(p.position, ix, iy, iz)) {
            cellToParticles[geo.grid.flatIndex(ix, iy, iz)].push_back(i);
        }
    }

    fpreal h2 = smoothingLength * smoothingLength;

    for (const auto& wp : myWindParticles) {
        int ix, iy, iz;
        if (!geo.grid.worldToGrid(wp.pos, ix, iy, iz)) continue;

        // Search neighboring voxel cells for sandstone particles.
        int searchRadius = (int)std::ceil(smoothingLength / geo.grid.dx);
        for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
            for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
                for (int dz = -searchRadius; dz <= searchRadius; ++dz) {
                    int nix = ix + dx;
                    int niy = iy + dy;
                    int niz = iz + dz;
                    if (!geo.grid.inBounds(nix, niy, niz)) continue;

                    exint cellIdx = geo.grid.flatIndex(nix, niy, niz);
                    for (exint pIdx : cellToParticles[cellIdx]) {
                        auto& p = geo.particles[pIdx];
                        UT_Vector3 diff = p.position - wp.pos;
                        fpreal dist2 = diff.dot(diff);
                        if (dist2 > h2) continue;

                        fpreal weight = kernelW(std::sqrt(dist2));
                        if (weight <= 0) continue;

                        fpreal vMag = wp.vel.length();
                        fpreal dot = -p.normal.dot(wp.vel);
                        if (dot > 0) {
                            fpreal Wa = abrasionCoeff * vMag * dot * weight * dt;
                            p.erosionValue += Wa;
                            p.abrasionErosion += Wa;
                        }
                    }
                }
            }
        }
    }

    // 4. Cleanup: Remove particles that have left the wind domain
    //    (windMin/windMax computed above; same box used for emission).
    for (exint i = (exint)myWindParticles.size() - 1; i >= 0; --i) {
        const auto& pos = myWindParticles[i].pos;
        if (pos.x() < windMin.x() || pos.x() > windMax.x() ||
            pos.y() < windMin.y() || pos.y() > windMax.y() ||
            pos.z() < windMin.z() || pos.z() > windMax.z()) {
            myWindParticles.removeIndex(i);
        }
    }
}

void WindSolver::emitWindParticles(const AreniteGeometry& geo, fpreal dt,
                                   const UT_Vector3& minBound,
                                   const UT_Vector3& maxBound) {
    (void)geo; // bounds are precomputed by computeAbrasion
    UT_Vector3 windDir = windDirection;
    windDir.normalize();

    // Probabilistic face selection based on wind direction components.
    fpreal ax = SYSabs(windDir.x());
    fpreal ay = SYSabs(windDir.y());
    fpreal az = SYSabs(windDir.z());
    fpreal totalFlux = ax + ay + az;

    if (totalFlux < 1e-6) return;
    if (windSpeed <= 0)   return;

    // Number of particles to emit this step.  The heuristic rate scales
    // with windSpeed * dt, but we floor at 1 whenever wind is active so
    // that very low windSpeed * dt doesn't truncate to zero (which would
    // give the appearance of no abrasion at all).
    int emitCount = (int)(1000 * dt * windSpeed);
    if (emitCount < 1) emitCount = 1;

    // Nudge spawn positions a hair INSIDE the wind-domain box so that
    // the cleanup pass (which uses strict `< minBound` / `> maxBound`
    // tests) cannot remove a freshly-emitted particle on the same step
    // due to floating-point rounding on the boundary face.
    const fpreal eps = 1e-4;

    for (int i = 0; i < emitCount; ++i) {
        WindParticle wp;
        
        // Pick a face proportional to the flux along that axis.
        fpreal r = (fpreal)rand() / RAND_MAX * totalFlux;
        
        if (r < ax) { // X-face
            wp.pos.x() = (windDir.x() > 0) ? minBound.x() + eps : maxBound.x() - eps;
            wp.pos.y() = minBound.y() + (fpreal)rand() / RAND_MAX * (maxBound.y() - minBound.y());
            wp.pos.z() = minBound.z() + (fpreal)rand() / RAND_MAX * (maxBound.z() - minBound.z());
        }
        else if (r < ax + ay) { // Y-face
            wp.pos.y() = (windDir.y() > 0) ? minBound.y() + eps : maxBound.y() - eps;
            wp.pos.x() = minBound.x() + (fpreal)rand() / RAND_MAX * (maxBound.x() - minBound.x());
            wp.pos.z() = minBound.z() + (fpreal)rand() / RAND_MAX * (maxBound.z() - minBound.z());
        }
        else { // Z-face
            wp.pos.z() = (windDir.z() > 0) ? minBound.z() + eps : maxBound.z() - eps;
            wp.pos.x() = minBound.x() + (fpreal)rand() / RAND_MAX * (maxBound.x() - minBound.x());
            wp.pos.y() = minBound.y() + (fpreal)rand() / RAND_MAX * (maxBound.y() - minBound.y());
        }
        
        wp.vel = windDir * windSpeed;
        
        // Add some turbulence.
        if (turbulence > 0) {
            wp.vel.x() += (fpreal)rand() / RAND_MAX * 2.0 * turbulence - turbulence;
            wp.vel.y() += (fpreal)rand() / RAND_MAX * 2.0 * turbulence - turbulence;
            wp.vel.z() += (fpreal)rand() / RAND_MAX * 2.0 * turbulence - turbulence;
        }

        myWindParticles.append(wp);
    }
}

void WindSolver::updateWindParticles(const AreniteGeometry& geo, fpreal dt) {
    if (myWindParticles.isEmpty()) return;

    // 1. Compute Densities.
    for (auto& i : myWindParticles) {
        i.density = 0;
        for (const auto& j : myWindParticles) {
            fpreal r = (i.pos - j.pos).length();
            i.density += particleMass * kernelW(r);
        }
        // Pressure via EOS: P = k * (rho - rho0)
        i.pressure = 50.0 * (i.density - restDensity);
        if (i.pressure < 0) i.pressure = 0;
    }

    // 2. Compute Forces (Pressure + Viscosity).
    for (auto& i : myWindParticles) {
        UT_Vector3 force(0, 0, 0);
        for (const auto& j : myWindParticles) {
            if (&i == &j) continue;

            UT_Vector3 r_vec = i.pos - j.pos;
            fpreal r = r_vec.length();
            if (r > smoothingLength || r < 1e-6) continue;

            // Pressure force.
            fpreal pTerm = (i.pressure / (i.density * i.density) + j.pressure / (j.density * j.density));
            force -= particleMass * pTerm * kernelGradW(r_vec);

            // Viscosity force (simplified).
            UT_Vector3 v_diff = j.vel - i.vel;
            force += viscosity * particleMass * (v_diff / j.density) * kernelW(r);
        }

        // 3. Advect and handle collisions with sandstone.
        UT_Vector3 accel = force / i.density;
        i.vel += accel * dt;
        i.pos += i.vel * dt;

        // Collision with sandstone grid.
        int ix, iy, iz;
        if (geo.grid.worldToGrid(i.pos, ix, iy, iz)) {
            exint idx = geo.grid.flatIndex(ix, iy, iz);
            if (geo.grid.cells[idx].occupied) {
                // Reflect velocity and push out.
                i.vel *= -0.5; // inelastic collision
                i.pos -= i.vel * dt; // push back
            }
        }
    }
}

fpreal WindSolver::kernelW(fpreal r) const {
    fpreal q = r / smoothingLength;
    if (q >= 1.0) return 0.0;
    
    // Cubic spline kernel (simplified constant).
    fpreal sigma = 8.0 / (M_PI * smoothingLength * smoothingLength * smoothingLength);
    if (q <= 0.5) {
        return sigma * (6.0 * (q * q * q - q * q) + 1.0);
    } else {
        return sigma * 2.0 * pow(1.0 - q, 3);
    }
}

UT_Vector3 WindSolver::kernelGradW(const UT_Vector3& r_vec) const {
    fpreal r = r_vec.length();
    if (r < 1e-6 || r > smoothingLength) return UT_Vector3(0, 0, 0);

    fpreal q = r / smoothingLength;
    fpreal sigma = 8.0 / (M_PI * pow(smoothingLength, 4));
    fpreal gradQ;

    if (q <= 0.5) {
        gradQ = sigma * 6.0 * (3.0 * q * q - 2.0 * q);
    } else {
        gradQ = -sigma * 6.0 * pow(1.0 - q, 2);
    }

    return (r_vec / r) * gradQ;
}
