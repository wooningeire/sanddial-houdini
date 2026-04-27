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

    for (auto& p : geo.particles) {
        if (p.isEroded || !p.isSurface) continue;

        fpreal trSigma = p.stressTensor(0, 0)
                        + p.stressTensor(1, 1)
                        + p.stressTensor(2, 2);

        fpreal Fcn = cohesion + frictionCoeff * trSigma;
        fpreal Wd = deflationCoeff * SYSexp(-(Fcn * Fcn) / twoAlphaSq);

        p.erosionValue += Wd;
    }
}

void WindSolver::computeAbrasion(AreniteGeometry& geo, fpreal dt) {
    // 1. Emit new wind particles at the upstream boundary.
    emitWindParticles(geo, dt);

    // 2. Update wind particle dynamics (SPH step).
    updateWindParticles(geo, dt);

    // 3. Accumulate abrasion on sandstone surface particles (Eq. 8).
    // Eq. 8: W_a = k_a * ||v|| * (-n . v)+
    
    // Optimization: Build a temporary map from voxel cells to sandstone particles.
    if (geo.grid.cells.isEmpty() || geo.grid.dx < 1e-6) return;

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
                        }
                    }
                }
            }
        }
    }

    // 4. Cleanup: Remove particles outside the expanded domain.
    if (geo.grid.dx < 1e-6) return;
    UT_Vector3 minBound = geo.grid.origin - geo.domainPadding * WIND_DOMAIN_EXPANSION;
    UT_Vector3 maxBound = geo.grid.origin + UT_Vector3(geo.grid.res[0] * geo.grid.dx, 
                                                       geo.grid.res[1] * geo.grid.dx, 
                                                       geo.grid.res[2] * geo.grid.dx) 
                          + geo.domainPadding * WIND_DOMAIN_EXPANSION;

    for (exint i = (exint)myWindParticles.size() - 1; i >= 0; --i) {
        const auto& pos = myWindParticles[i].pos;
        if (pos.x() < minBound.x() || pos.x() > maxBound.x() ||
            pos.y() < minBound.y() || pos.y() > maxBound.y() ||
            pos.z() < minBound.z() || pos.z() > maxBound.z()) {
            myWindParticles.removeIndex(i);
        }
    }
}

void WindSolver::emitWindParticles(const AreniteGeometry& geo, fpreal dt) {
    // Determine the emission plane based on wind direction.
    // For simplicity, we'll emit from the min-X or max-X face depending on windDir.x.
    UT_Vector3 windDir = windDirection;
    windDir.normalize();

    // Expansion: 2x the sandstone domain.
    UT_Vector3 size(geo.grid.res[0] * geo.grid.dx, geo.grid.res[1] * geo.grid.dx, geo.grid.res[2] * geo.grid.dx);
    UT_Vector3 center = geo.grid.origin + size * 0.5;
    UT_Vector3 expandedSize = size * WIND_DOMAIN_EXPANSION;
    UT_Vector3 minBound = center - expandedSize * 0.5;
    UT_Vector3 maxBound = center + expandedSize * 0.5;

    // Number of particles to emit this step.
    int emitCount = (int)(1000 * dt * windSpeed); // Heuristic rate
    
    for (int i = 0; i < emitCount; ++i) {
        WindParticle wp;
        // Emit from the "upstream" boundary. 
        // This is a simplified emission logic: pick a random point on the boundary box.
        // For a more directional wind, we should pick the face that windDir points from.
        if (windDir.x() > 0) wp.pos.x() = minBound.x();
        else wp.pos.x() = maxBound.x();

        wp.pos.y() = minBound.y() + (fpreal)rand() / RAND_MAX * (maxBound.y() - minBound.y());
        wp.pos.z() = minBound.z() + (fpreal)rand() / RAND_MAX * (maxBound.z() - minBound.z());
        
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
