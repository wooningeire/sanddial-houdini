#pragma once

#include "AreniteGeometry.h"

/// Computes fluvial (water) erosion values per particle using a 3D
/// generalisation of the FastFlow flow-routing algorithm.
///
/// FastFlow routes precipitation through the particle surface to compute
/// fluvial discharge, which is combined with slope and critical shear stress
/// to determine the erosion rate at each particle.
class WaterSolver {
public:
    WaterSolver() = default;
    ~WaterSolver() = default;

    // ── Parameters ──────────────────────────────────────────────────────────
    /// Precipitation rate (uniform, as a starting point).
    fpreal precipitation = 1.0;

    /// Critical shear stress for sediment entrainment.
    fpreal criticalShearStress = 0.05;

    /// Compute per-particle fluvial erosion and accumulate into
    /// `AreniteParticle::erosionValue`.
    void solve(AreniteGeometry& geo, fpreal dt);

private:
    /// Build the flow routing graph from the particle normals and slope data.
    void buildFlowGraph(const AreniteGeometry& geo);

    /// Route water through the graph and compute fluvial discharge per cell.
    void computeDischarge();

    /// Convert discharge values into erosion rates and accumulate.
    void accumulateErosion(AreniteGeometry& geo);
};
