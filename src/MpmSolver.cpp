#include "MpmSolver.h"
#include <SYS/SYS_Math.h>
#include <UT/UT_Matrix3.h>

// Define this to make particles dynamic (gravity, boundary, advection)
// for debugging MPM behavior. Undefine for normal Arenite (static sandstone).
// #define SANDDIAL_MPM_DEBUG_DYNAMIC

// ── Helper: outer product of two UT_Vector3 → UT_Matrix3 ──────────────────
static UT_Matrix3 outerProduct(const UT_Vector3& a, const UT_Vector3& b) {
    // UT_Matrix3 is row-major:  M(row, col)
    return UT_Matrix3(
        a.x()*b.x(), a.x()*b.y(), a.x()*b.z(),
        a.y()*b.x(), a.y()*b.y(), a.y()*b.z(),
        a.z()*b.x(), a.z()*b.y(), a.z()*b.z()
    );
}

// ── Helper: polar decomposition  F = R * S ─────────────────────────────────
// Uses the iterative method: R = F * (F^T F)^{-1/2}
// For our purposes a few iterations suffice.
static void polarDecomp(const UT_Matrix3& F, UT_Matrix3& R, UT_Matrix3& S) {
    // Start with R = F, then iterate R = 0.5*(R + R^{-T})
    R = F;
    for (int iter = 0; iter < 10; ++iter) {
        UT_Matrix3 Rt;
        Rt = R;
        Rt.transpose();
        UT_Matrix3 RtInv;
        RtInv = Rt;
        if (RtInv.invert() != 0) {
            // Singular — just use identity
            R.identity();
            S = F;
            return;
        }
        // R = 0.5 * (R + R^{-T})
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                R(i, j) = 0.5 * (R(i, j) + RtInv(i, j));
    }
    // S = R^T * F
    UT_Matrix3 Rt = R;
    Rt.transpose();
    S = Rt;
    S *= F;  // This does S = Rt * F ... but UT_Matrix3 *= is right-multiply
    // Actually: S = R^T * F.  With UT_Matrix3, operator*= does this *= rhs,
    // i.e. S = S * F.  Since S was set to R^T, S = R^T * F.  But UT_Matrix3
    // row-vector convention means M *= N  =>  M = M * N.  Let me be explicit:
    S.zero();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                S(i, j) += Rt(i, k) * F(k, j);
}

// ── Lamé parameters ────────────────────────────────────────────────────────
void MpmSolver::computeLame(fpreal& mu, fpreal& lambda) const {
    mu     = youngModulus / (2.0 * (1.0 + poissonRatio));
    lambda = youngModulus * poissonRatio
           / ((1.0 + poissonRatio) * (1.0 - 2.0 * poissonRatio));
}

// ── Quadratic B-spline weights ─────────────────────────────────────────────
void MpmSolver::quadraticWeights(fpreal fx, fpreal w[3]) {
    w[0] = 0.5 * (1.5 - fx) * (1.5 - fx);
    w[1] = 0.75 - (fx - 1.0) * (fx - 1.0);
    w[2] = 0.5 * (fx - 0.5) * (fx - 0.5);
}

// ── Solve ──────────────────────────────────────────────────────────────────
void MpmSolver::solve(AreniteGeometry& geo, fpreal dt) {
    if (geo.particles.size() == 0 || dt <= 0) return;

    fpreal mu, lambda;
    computeLame(mu, lambda);

    // Auto-compute volume and mass from grid spacing.
    fpreal dx = geo.grid.dx;
    particleVolume = dx * dx * dx;

    // Use a realistic sandstone density (~2000 kg/m³).
    fpreal density = 2000.0;
    particleMass = density * particleVolume;

    // CFL condition based on elastic wave speed:
    //   c = sqrt((lambda + 2*mu) / density)
    //   dt_max = cfl_factor * dx / c
    fpreal waveSpeed = SYSsqrt((lambda + 2.0 * mu) / density);
    if (waveSpeed < 1e-10) waveSpeed = 1.0;
    fpreal cflFactor = 0.4;
    fpreal maxSubDt = cflFactor * dx / waveSpeed;
    int nSubSteps = SYSmax(1, (int)SYSceil(dt / maxSubDt));
    fpreal subDt = dt / (fpreal)nSubSteps;

    for (int step = 0; step < nSubSteps; ++step) {
        geo.grid.clear();
        transferToGrid(geo, subDt, mu, lambda);
        updateGrid(geo, subDt);
        transferToParticles(geo, subDt, mu, lambda);
    }
}

// ── P2G ────────────────────────────────────────────────────────────────────
void MpmSolver::transferToGrid(AreniteGeometry& geo, fpreal dt,
                               fpreal mu, fpreal lambda) {
    VoxelGrid& g = geo.grid;
    fpreal inv_dx = 1.0 / g.dx;

    for (auto& p : geo.particles) {
        if (p.isEroded) continue;

        // Base cell coordinate
        UT_Vector3 gridPos = (p.position - g.origin) * inv_dx;
        int base[3] = {
            (int)SYSfloor(gridPos.x() - 0.5),
            (int)SYSfloor(gridPos.y() - 0.5),
            (int)SYSfloor(gridPos.z() - 0.5)
        };
        UT_Vector3 fx(gridPos.x() - base[0],
                      gridPos.y() - base[1],
                      gridPos.z() - base[2]);

        // Quadratic B-spline weights per axis
        fpreal wx[3], wy[3], wz[3];
        quadraticWeights(fx.x(), wx);
        quadraticWeights(fx.y(), wy);
        quadraticWeights(fx.z(), wz);

        // Fixed corotated stress:  stress = 2*mu*(F-R)*F^T + lambda*(J-1)*J*I
        UT_Matrix3 R, S;
        polarDecomp(p.deformationGrad, R, S);
        fpreal J = p.deformationGrad.determinant();

        UT_Matrix3 Ft = p.deformationGrad;
        Ft.transpose();

        // Compute  PF^T = 2*mu*(F-R)*F^T + lambda*(J-1)*J * I
        // Then stress_term = -vol * PF^T * (4 * inv_dx^2 * dt)
        UT_Matrix3 FminusR = p.deformationGrad;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                FminusR(i, j) -= R(i, j);

        // PFt = 2*mu*(F-R)*F^T
        UT_Matrix3 PFt;
        PFt.zero();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    PFt(i, j) += FminusR(i, k) * Ft(k, j);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                PFt(i, j) *= 2.0 * mu;

        // Add lambda*(J-1)*J to diagonal
        fpreal ljj = lambda * (J - 1.0) * J;
        PFt(0, 0) += ljj;
        PFt(1, 1) += ljj;
        PFt(2, 2) += ljj;

        // Save the Cauchy stress for the erosion solver:
        //   sigma = (1/J) * P * F^T   (but we already have P*F^T above)
        if (SYSabs(J) > 1e-10) {
            p.stressTensor = PFt;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    p.stressTensor(i, j) /= J;
        }

        // stress_term = -4 * inv_dx^2 * dt * vol * PFt
        fpreal scale = -4.0 * inv_dx * inv_dx * dt * particleVolume;
        UT_Matrix3 stress_term = PFt;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                stress_term(i, j) *= scale;

        // affine = stress_term + mass * C
        UT_Matrix3 affine = stress_term;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                affine(i, j) += particleMass * p.apicC(i, j);

        // Scatter to 3x3x3 neighborhood
        for (int di = 0; di < 3; ++di) {
            for (int dj = 0; dj < 3; ++dj) {
                for (int dk = 0; dk < 3; ++dk) {
                    int ix = base[0] + di;
                    int iy = base[1] + dj;
                    int iz = base[2] + dk;
                    if (!g.inBounds(ix, iy, iz)) continue;

                    fpreal weight = wx[di] * wy[dj] * wz[dk];
                    UT_Vector3 dpos((di - fx.x()) * g.dx,
                                   (dj - fx.y()) * g.dx,
                                   (dk - fx.z()) * g.dx);

                    // momentum contribution = weight * (mass*v + affine*dpos)
                    UT_Vector3 affineDpos(
                        affine(0,0)*dpos.x() + affine(0,1)*dpos.y() + affine(0,2)*dpos.z(),
                        affine(1,0)*dpos.x() + affine(1,1)*dpos.y() + affine(1,2)*dpos.z(),
                        affine(2,0)*dpos.x() + affine(2,1)*dpos.y() + affine(2,2)*dpos.z()
                    );

                    exint idx = g.flatIndex(ix, iy, iz);
                    g.cells[idx].momentum += weight * (p.velocity * particleMass + affineDpos);
                    g.cells[idx].mass     += weight * particleMass;
                    g.cells[idx].occupied  = true;
                }
            }
        }
    }
}

// ── Grid update ────────────────────────────────────────────────────────────
void MpmSolver::updateGrid(AreniteGeometry& geo, fpreal dt) {
    VoxelGrid& g = geo.grid;

    for (exint idx = 0; idx < g.cells.size(); ++idx) {
        VoxelCell& c = g.cells[idx];
        if (c.mass > 0) {
            c.velocity = c.momentum / c.mass;

#ifdef SANDDIAL_MPM_DEBUG_DYNAMIC
            // Apply gravity
            c.velocity.y() += dt * (-1.0);

            // Recover (ix, iy, iz) from flat index for boundary check
            int ix = (int)(idx % g.res[0]);
            int iy = (int)((idx / g.res[0]) % g.res[1]);
            int iz = (int)(idx / ((exint)g.res[0] * g.res[1]));

            // Boundary thickness in cells
            int bnd = SYSmax(2, g.res[0] / 20);

            // Sticky walls (left, right, top, front, back)
            if (ix < bnd || ix >= g.res[0] - bnd ||
                iy >= g.res[1] - bnd ||
                iz < bnd || iz >= g.res[2] - bnd) {
                c.velocity = UT_Vector3(0, 0, 0);
            }
            // Separating floor
            if (iy < bnd) {
                c.velocity.y() = SYSmax((fpreal)0.0, c.velocity.y());
            }
#endif
        }
    }
}

// ── G2P ────────────────────────────────────────────────────────────────────
void MpmSolver::transferToParticles(AreniteGeometry& geo, fpreal dt,
                                    fpreal /*mu*/, fpreal /*lambda*/) {
    VoxelGrid& g = geo.grid;
    fpreal inv_dx = 1.0 / g.dx;

    for (auto& p : geo.particles) {
        if (p.isEroded) continue;

        UT_Vector3 gridPos = (p.position - g.origin) * inv_dx;
        int base[3] = {
            (int)SYSfloor(gridPos.x() - 0.5),
            (int)SYSfloor(gridPos.y() - 0.5),
            (int)SYSfloor(gridPos.z() - 0.5)
        };
        UT_Vector3 fx(gridPos.x() - base[0],
                      gridPos.y() - base[1],
                      gridPos.z() - base[2]);

        fpreal wx[3], wy[3], wz[3];
        quadraticWeights(fx.x(), wx);
        quadraticWeights(fx.y(), wy);
        quadraticWeights(fx.z(), wz);

        p.velocity = UT_Vector3(0, 0, 0);
        p.apicC.zero();

        for (int di = 0; di < 3; ++di) {
            for (int dj = 0; dj < 3; ++dj) {
                for (int dk = 0; dk < 3; ++dk) {
                    int ix = base[0] + di;
                    int iy = base[1] + dj;
                    int iz = base[2] + dk;
                    if (!g.inBounds(ix, iy, iz)) continue;

                    fpreal weight = wx[di] * wy[dj] * wz[dk];
                    UT_Vector3 dpos(di - fx.x(), dj - fx.y(), dk - fx.z());
                    UT_Vector3 grid_v = g.cells[g.flatIndex(ix, iy, iz)].velocity;

                    p.velocity += weight * grid_v;

                    // APIC C = sum  4 * inv_dx * outer(weight * grid_v, dpos)
                    UT_Vector3 wgv = weight * grid_v;
                    UT_Matrix3 op = outerProduct(wgv, dpos);
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c)
                            p.apicC(r, c) += 4.0 * inv_dx * op(r, c);
                }
            }
        }

        // MLS-MPM deformation gradient update:  F = (I + dt * C) * F
        UT_Matrix3 update;
        update.identity();
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                update(r, c) += dt * p.apicC(r, c);

        // newF = update * F
        UT_Matrix3 newF;
        newF.zero();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    newF(i, j) += update(i, k) * p.deformationGrad(k, j);

        p.deformationGrad = newF;

#ifdef SANDDIAL_MPM_DEBUG_DYNAMIC
        // Debug: advect particles so we can see MPM dynamics
        p.position += p.velocity * dt;
#endif
        // In production (no debug flag), particles are static;
        // only eroded particles move via DepositionSolver.
    }
}
