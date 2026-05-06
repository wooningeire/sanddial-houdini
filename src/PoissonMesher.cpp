#include "PoissonMesher.h"

// PoissonRecon headers — order matters
#include "PreProcessor.h"
#include "Reconstructors.h"

#include <GU/GU_Detail.h>
#include <GA/GA_Handle.h>
#include <GU/GU_PrimPoly.h>
#include <SYS/SYS_Math.h>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace PoissonRecon;

/// True for live sediment within the same height band used in
/// NormalsSolver::groundContactLayer and in buildGroundPhantoms.
/// Lets the point stream admit those grains even if isSurface was not
/// recomputed yet in the same frame (defensive).
static bool sedimentInGroundMeshBand(const AreniteGeometry& geo,
                                      const AreniteParticle& p)
{
    if (!geo.useGroundPlane || p.isEroded || !p.isSediment) return false;
    fpreal dx = (fpreal)geo.grid.dx;
    if (!(dx > 0)) dx = (fpreal)geo.voxelSize;
    if (!(dx > 0)) return false;
    fpreal dy = (fpreal)p.position.y() - geo.groundY;
    fpreal band = SYSmax((fpreal)20.0 * dx, (fpreal)0.15);
    fpreal below = SYSmax((fpreal)2.0 * dx, (fpreal)0.02);
    return dy >= -below && dy <= band;
}

// ── Input stream: feeds Sanddial particles as oriented samples ──────────────
//
// The stream emits two phases of oriented samples for sediment-only
// meshing:
//
//   1. Real-particle splats.  Each accepted sediment particle is
//      "splatted" into a small ring of oriented samples in the
//      tangent plane to its normal.  A bare particle is just one
//      oriented sample, which screened Poisson cannot reconstruct
//      cleanly: isolated halo particles either disappear below the
//      iso-value or shatter into per-sample fragments.  Splatting
//      gives every particle a few-voxel disk of consistent samples,
//      so the reconstructor sees a locally dense cloud and produces
//      one connected surface patch per particle that fuses with its
//      neighbors.  Splats use fixed angles for determinism (no RNG)
//      and all share the parent particle's normal, so they reinforce
//      -- not noise up -- the indicator gradient.
//
//   2. Phantom ground samples.  A second pass emits one oriented
//      sample per X-Z grid cell of the cloud's near-ground footprint,
//      placed exactly at groundY with normal (0, -1, 0).  Without
//      these the field has no boundary condition under the cloud:
//      sediment normals all point upward, so screened Poisson is free
//      to extrapolate a closure surface either far below ground (a
//      lobe that flattens into a wide pancake when clamped) or
//      slightly above ground per isolated knob (a floating dome).
//      Anchoring the field at groundY makes the iso-surface close on
//      the ground exactly where there is sediment overhead, and stops
//      it extrapolating where there isn't.
struct ArenitePointStream
    : public Reconstructor::InputOrientedSampleStream<float, 3>
{
    const AreniteGeometry& geo;
    MeshFilter filter;
    exint idx;
    int splatIdx;             ///< Current splat sample within the active particle.
    int splatsPerParticle;    ///< 1 = no splatting; otherwise 1 center + (N-1) ring.
    float splatRadius;        ///< Ring radius in world units.

    /// Pre-computed phantom ground-plane positions, one per X-Z
    /// footprint cell of the near-ground sediment cloud.  Drained in
    /// `read` after every real-particle splat has been emitted.
    std::vector<UT_Vector3> groundPhantoms;
    exint phantomIdx;

    ArenitePointStream(const AreniteGeometry& g, MeshFilter f)
        : geo(g), filter(f), idx(0), splatIdx(0)
        , splatsPerParticle(1), splatRadius(0.0f)
        , phantomIdx(0)
    {
        if (filter == MeshFilter::SedimentOnly) {
            splatsPerParticle = 9; // 1 center + 8 ring samples
            // Ring radius ~1.0 voxel: ~2-voxel disk diameter.  Wider disks
            // (1.25*dx) plus optional downstream subdiv read as a bulkier
            // hull than the particle footprint and bridge into lobes.
            float dx = (float)geo.grid.dx;
            if (!(dx > 0)) dx = (float)geo.voxelSize;
            if (!(dx > 0)) dx = 0.025f;
            splatRadius = 1.0f * dx;

            if (geo.useGroundPlane) buildGroundPhantoms();
        }
    }

    /// Build the deduped set of X-Z footprint cells for the near-
    /// ground sediment cloud and emit one phantom ground position per
    /// cell.  A particle is considered "near ground" if it sits within
    /// `proximity` of `geo.groundY`; sediment higher than that is not
    /// anchored to the ground.
    ///
    /// Each particle stamps only the single grid cell it occupies (no
    /// 3x3 dilation).  A wide dilation fused unrelated phantom sites
    /// into one enormous flat sheet on the floor that extended far
    /// beyond where any particles actually sat.
    void buildGroundPhantoms() {
        const fpreal dx = (fpreal)geo.grid.dx;
        if (!(dx > 0)) return;
        // Match NormalsSolver: min height band, then at least 15 cm so tiny
        // dx does not exclude resting grains / phantom anchors.
        const fpreal proximity = SYSmax(20.0 * dx, (fpreal)0.15);
        const fpreal belowSlack = SYSmax(2.0 * dx, (fpreal)0.02);

        // Lowest sediment Y per X-Z column (grid cell) for near-ground grains.
        std::unordered_map<uint64_t, fpreal> colMinY;
        for (exint i = 0; i < geo.particles.entries(); ++i) {
            const auto& p = geo.particles(i);
            if (p.isEroded || !p.isSediment) continue;
            fpreal dy = (fpreal)(p.position.y() - geo.groundY);
            if (dy < -belowSlack || dy > proximity) continue;

            int ix = (int)std::floor((fpreal)(p.position.x() - geo.grid.origin.x()) / dx);
            int iz = (int)std::floor((fpreal)(p.position.z() - geo.grid.origin.z()) / dx);
            uint64_t key = ((uint64_t)(uint32_t)ix << 32)
                         | (uint64_t)(uint32_t)iz;
            fpreal y = (fpreal)p.position.y();
            auto it = colMinY.find(key);
            if (it == colMinY.end())
                colMinY.emplace(key, y);
            else if (y < it->second)
                it->second = y;
        }

        groundPhantoms.reserve(colMinY.size());
        for (const auto& kv : colMinY) {
            uint64_t key = kv.first;
            const fpreal lo = kv.second;
            int ix = (int)(int32_t)(key >> 32);
            int iz = (int)(int32_t)(key & 0xFFFFFFFFu);
            fpreal cx = geo.grid.origin.x() + ((fpreal)ix + 0.5) * dx;
            fpreal cz = geo.grid.origin.z() + ((fpreal)iz + 0.5) * dx;
            // Lift the phantom slightly into the column between groundY
            // and the lowest grain so screened Poisson cannot leave a
            // vacuum band (particles visibly under the mesh lip).
            fpreal lift = SYSmax((fpreal)0, lo - geo.groundY);
            fpreal py = geo.groundY + (fpreal)0.1 * lift;
            py = SYSmin(py, lo - (fpreal)0.08 * dx);
            py = SYSmax(py, geo.groundY);
            groundPhantoms.push_back(UT_Vector3(cx, py, cz));
        }
    }

    void reset() override { idx = 0; splatIdx = 0; phantomIdx = 0; }

    /// Whether @p part should contribute samples for the current filter.
    bool accepts(const AreniteParticle& part) const {
        if (part.isEroded) return false;
        if (filter == MeshFilter::SandstoneOnly && part.isSediment) return false;
        if (filter == MeshFilter::SedimentOnly && !part.isSediment) return false;
        if (!part.isSurface &&
            !(filter == MeshFilter::SedimentOnly &&
              sedimentInGroundMeshBand(geo, part)))
            return false;
        return true;
    }

    bool read(Point<float, 3>& p, Point<float, 3>& n) override {
        while (idx < geo.particles.entries()) {
            const auto& part = geo.particles(idx);
            if (!accepts(part)) {
                ++idx;
                splatIdx = 0;
                continue;
            }

            UT_Vector3 normal = part.normal;
            fpreal nlen = normal.length();
            if (nlen < 1e-8) normal = UT_Vector3(0, 1, 0);
            else             normal /= nlen;

            UT_Vector3 pos = part.position;

            if (filter == MeshFilter::SedimentOnly && geo.useGroundPlane) {
                fpreal dxg = (fpreal)geo.grid.dx;
                if (!(dxg > 0)) dxg = (fpreal)geo.voxelSize;
                fpreal dy = part.position.y() - geo.groundY;
                fpreal band = SYSmax((fpreal)20.0 * dxg, (fpreal)0.15);
                fpreal below = SYSmax((fpreal)2.0 * dxg, (fpreal)0.02);
                if (dy >= -below && dy <= band) {
                    fpreal slack = SYSmax((fpreal)0, dy);
                    fpreal pull =
                        SYSmin((fpreal)splatRadius, slack) * (fpreal)0.55;
                    pull = SYSmax(pull, (fpreal)0.22 * splatRadius);
                    pos.y() -= pull;
                }
            }

            // splatIdx == 0 emits the particle itself; splatIdx > 0
            // emits one of the ring samples.
            if (splatsPerParticle > 1 && splatIdx > 0) {
                UT_Vector3 anchor = (SYSabs(normal.x()) > 0.9)
                                  ? UT_Vector3(0, 1, 0)
                                  : UT_Vector3(1, 0, 0);
                UT_Vector3 t1 = cross(normal, anchor);
                t1.normalize();
                UT_Vector3 t2 = cross(normal, t1);

                int ring = splatIdx - 1;
                int ringCount = splatsPerParticle - 1;
                fpreal ang = (2.0 * M_PI) * (fpreal)ring / (fpreal)ringCount;
                pos += (SYScos(ang) * t1 + SYSsin(ang) * t2) * splatRadius;
            }

            // Shift every splat sample a short step along -n (into the
            // reconstructed solid).  With upward-pointing normals the
            // unconstrained screened field otherwise tends to sit *above*
            // the particle positions, so the mesh visually floats over
            // the point cloud (especially on knobs and thin halos).
            if (filter == MeshFilter::SedimentOnly)
                pos -= normal * ((fpreal)0.38 * splatRadius);

            p[0] = (float)pos.x();
            p[1] = (float)pos.y();
            p[2] = (float)pos.z();
            n[0] = (float)normal.x();
            n[1] = (float)normal.y();
            n[2] = (float)normal.z();

            ++splatIdx;
            if (splatIdx >= splatsPerParticle) {
                ++idx;
                splatIdx = 0;
            }
            return true;
        }

        // Phase 2: phantom "sole" samples with downward normals.  Y is
        // lifted part-way from groundY toward each column's lowest grain
        // (buildGroundPhantoms) so the implicit field bridges the gap
        // between floor particles and a lip that used to hover above them.
        if (phantomIdx < (exint)groundPhantoms.size()) {
            const UT_Vector3& pos = groundPhantoms[(size_t)phantomIdx++];
            p[0] = (float)pos.x();
            p[1] = (float)pos.y();
            p[2] = (float)pos.z();
            n[0] = 0.0f;
            n[1] = -1.0f;
            n[2] = 0.0f;
            return true;
        }
        return false;
    }
};

// ── Output streams: collect vertices and polygons into vectors ───────────────
struct MeshVertexStream
    : public Reconstructor::OutputLevelSetVertexStream<float, 3>
{
    std::vector<float>& coords;

    MeshVertexStream(std::vector<float>& c) : coords(c) {}

    size_t size() const override { return coords.size() / 3; }

    size_t write(const Point<float, 3>& p,
                 const Point<float, 3>& /*gradient*/,
                 const float& /*weight*/) override
    {
        for (unsigned d = 0; d < 3; ++d) coords.push_back(p[d]);
        return coords.size() / 3 - 1;
    }
};

struct MeshFaceStream : public Reconstructor::OutputFaceStream<2>
{
    std::vector<std::vector<int>>& polys;

    MeshFaceStream(std::vector<std::vector<int>>& p) : polys(p) {}

    size_t size() const override { return polys.size(); }

    size_t write(const std::vector<node_index_type>& polygon) override {
        std::vector<int> face(polygon.size());
        for (size_t i = 0; i < polygon.size(); ++i)
            face[i] = (int)polygon[i];
        polys.push_back(std::move(face));
        return polys.size() - 1;
    }
};

// ── Reconstruction entry point ──────────────────────────────────────────────
void PoissonMesher::reconstruct(const AreniteGeometry& geo,
                                GU_Detail* outputGeo,
                                int depth,
                                float scale,
                                MeshFilter filter)
{
    if (!outputGeo) return;

    // Count eligible particles (alive + surface + filter).  The
    // gating must mirror ArenitePointStream::accepts so we only bail
    // out of meshing when the stream would actually be empty.
    int eligibleCount = 0;
    for (exint i = 0; i < geo.particles.entries(); ++i) {
        const auto& p = geo.particles(i);
        if (p.isEroded) continue;
        if (filter == MeshFilter::SandstoneOnly && p.isSediment) continue;
        if (filter == MeshFilter::SedimentOnly && !p.isSediment) continue;
        if (!p.isSurface &&
            !(filter == MeshFilter::SedimentOnly &&
              sedimentInGroundMeshBand(geo, p)))
            continue;
        ++eligibleCount;
    }
    if (eligibleCount < 4) return; // need at least a few points

    // ── Configure solver ────────────────────────────────────────────────
    using ReconType = Reconstructor::Poisson;
    static const unsigned int FEMSig =
        FEMDegreeAndBType<ReconType::DefaultFEMDegree,
                          ReconType::DefaultFEMBoundary>::Signature;
    using FEMSigs = IsotropicUIntPack<3, FEMSig>;

    ReconType::SolutionParameters<float> solverParams;
    solverParams.depth   = (unsigned int)depth;
    solverParams.scale   = scale;
    solverParams.verbose = false;

    // Sediment-only meshing relies on two features of
    // ArenitePointStream rather than solver-param overrides:
    //
    //   * Per-particle splatting (1 center + 8 ring samples in a
    //     ~2-voxel diameter tangent disk) gives each particle a
    //     locally dense neighborhood, so screened-Poisson's defaults
    //     can reconstruct a clean manifold patch around even an
    //     isolated outlier.  Particles within roughly 2 voxels fuse, those
    //     farther apart stay disconnected -- the indiscriminate
    //     bridging that earlier versions used (wide kernelDepth /
    //     high samplesPerNode) also connected unrelated regions
    //     through long stalactite lobes.
    //
    //   * Phantom ground samples placed at groundY underneath
    //     near-ground sediment supply a real boundary condition for
    //     the screened-Poisson field.  Without them, all sediment
    //     normals point upward and the field has nothing to anchor
    //     itself to underneath the cloud, so it either extrapolates
    //     a large subterranean lobe or leaves each isolated knob
    //     floating slightly above the ground.
    //
    // Sediment-only: screened-Poisson still rides above splats in
    // practice; high pointWeight + a strong inward sample nudge (see
    // ArenitePointStream::read) are the first line of defense, and
    // exactInterpolation was dropped — it often *reduced* visible change
    // with mixed upward splats + downward sole samples.
    if (filter == MeshFilter::SedimentOnly) {
        solverParams.pointWeight    = 14.0f;
        solverParams.samplesPerNode = 1.15f;
    }

    // ── Solve ───────────────────────────────────────────────────────────
    ArenitePointStream pointStream(geo, filter);

    using Implicit = Reconstructor::Implicit<float, 3, FEMSigs>;
    using Solver   = ReconType::Solver<float, 3, FEMSigs>;

    Implicit* implicit = Solver::Solve(pointStream, solverParams);
    if (!implicit) return;

    // ── Extract level set ───────────────────────────────────────────────
    Reconstructor::LevelSetExtractionParameters extractionParams;
    extractionParams.linearFit      = false;
    extractionParams.forceManifold  = true;
    extractionParams.polygonMesh    = false; // output triangles
    extractionParams.verbose        = false;

    std::vector<float> vCoords;
    std::vector<std::vector<int>> polygons;

    MeshVertexStream vStream(vCoords);
    MeshFaceStream   fStream(polygons);

    implicit->extractLevelSet(vStream, fStream, extractionParams);
    delete implicit;

    // ── Sediment near-floor vertex shear ───────────────────────────────
    //
    // When nothing above moves the iso-surface enough, shear the lower
    // hull downward in world Y only inside a slab above the ground plane.
    // This is deliberately local to the deposit's bottom so tall faces
    // of the pile are not collapsed.
    if (filter == MeshFilter::SedimentOnly && geo.useGroundPlane) {
        fpreal dx = (fpreal)geo.grid.dx;
        if (!(dx > 0)) dx = (fpreal)geo.voxelSize;
        if (!(dx > 0)) dx = (fpreal)0.025;
        const float gnd = (float)geo.groundY;
        // Thickness of Y-slab (from ground up) that gets sheared down.
        // Cap so a huge voxel dx does not drag the whole pile.
        const float bandThick = (float)SYSmin(
            SYSmax((fpreal)30.0 * dx, (fpreal)0.18), (fpreal)0.45);
        const float bandTop = gnd + bandThick;
        const float drop = (float)SYSmax((fpreal)0.85 * dx, (fpreal)0.015);
        for (size_t i = 0; i < vCoords.size() / 3; ++i) {
            float y = vCoords[3 * i + 1];
            if (y <= bandTop) vCoords[3 * i + 1] = y - drop;
        }
    }

    // ── Snap below-ground vertices up to the ground plane ──────────────
    //
    // Screened Poisson always closes the implicit surface, even where
    // the input cloud has no samples; with all upward-pointing
    // sediment normals the field can extrapolate slightly below the
    // lowest particles.  Earlier revisions tried to clip the mesh on
    // the ground plane, but any clip that splits or drops triangles
    // opens new boundary edges (visible as holes along the ground in
    // the final render).
    //
    // Clamping is a topology-preserving alternative: shift each
    // below-ground vertex up to the ground plane and leave every
    // triangle in place.  The mesh stays manifold-and-closed (which
    // is what `forceManifold = true` already produced), and any
    // strip of geometry that had extrapolated below ground collapses
    // into a thin flat cap exactly on the ground line.
    //
    // This works in practice because the upstream fixes (sediment-
    // aware isSurface flag, no interior-particle bias, default-width
    // kernel) keep the iso-surface tightly anchored to the actual
    // particle cloud, so there is no large subterranean lobe waiting
    // to be flattened into a pancake.
    if (geo.useGroundPlane) {
        const float groundY = (float)geo.groundY;
        const size_t numVerts = vCoords.size() / 3;
        for (size_t i = 0; i < numVerts; ++i) {
            float& y = vCoords[3 * i + 1];
            if (y < groundY) y = groundY;
        }
    }

    // ── Write into GU_Detail ────────────────────────────────────────────
    outputGeo->clearAndDestroy();

    size_t numVerts = vCoords.size() / 3;
    if (numVerts == 0 || polygons.empty()) return;

    // Compact: only emit vertices that referenced polygons actually
    // use, so any unreferenced output from PoissonRecon doesn't litter
    // the output detail.
    std::vector<int> remap(numVerts, -1);
    int nextIdx = 0;
    for (const auto& face : polygons)
        for (int vi : face)
            if (vi >= 0 && (size_t)vi < numVerts && remap[vi] < 0)
                remap[vi] = nextIdx++;

    if (nextIdx == 0) return;

    GA_Offset startPt = outputGeo->appendPointBlock((GA_Size)nextIdx);
    GA_RWHandleV3 posH(outputGeo->getP());
    for (size_t i = 0; i < numVerts; ++i) {
        if (remap[i] < 0) continue;
        GA_Offset pt = startPt + (GA_Offset)remap[i];
        posH.set(pt, UT_Vector3(vCoords[3*i], vCoords[3*i+1], vCoords[3*i+2]));
    }

    for (const auto& face : polygons) {
        GU_PrimPoly* prim = GU_PrimPoly::build(outputGeo, (int)face.size(),
                                                GU_POLY_CLOSED, 0);
        for (size_t v = 0; v < face.size(); ++v) {
            prim->setVertexPoint((int)v, startPt + (GA_Offset)remap[face[v]]);
        }
    }
}
