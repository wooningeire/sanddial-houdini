#include "PoissonMesher.h"

// PoissonRecon headers — order matters
#include "PreProcessor.h"
#include "Reconstructors.h"

#include <GU/GU_Detail.h>
#include <GA/GA_Handle.h>
#include <GU/GU_PrimPoly.h>
#include <SYS/SYS_Math.h>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace PoissonRecon;

// ── Input stream: feeds Sanddial particles as oriented samples ──────────────
//
// For sediment-only meshing the stream "splats" each particle as a small
// ring of oriented samples in the tangent plane to its normal.  A bare
// sediment particle is just one oriented sample, which screened Poisson
// cannot reconstruct cleanly: isolated halo particles either disappear
// below the iso-value or shatter into per-sample fragments.  Splatting
// gives every particle a few-voxel disk of consistent samples, so the
// reconstructor sees a locally dense cloud and produces one connected
// surface patch per particle that fuses with its neighbors.
//
// Splats are arranged at fixed angles for determinism (no RNG), and all
// share the parent particle's normal so they reinforce -- rather than
// noise up -- the indicator gradient.
struct ArenitePointStream
    : public Reconstructor::InputOrientedSampleStream<float, 3>
{
    const AreniteGeometry& geo;
    MeshFilter filter;
    exint idx;
    int splatIdx;             ///< Current splat sample within the active particle.
    int splatsPerParticle;    ///< 1 = no splatting; otherwise 1 center + (N-1) ring.
    float splatRadius;        ///< Ring radius in world units.

    ArenitePointStream(const AreniteGeometry& g, MeshFilter f)
        : geo(g), filter(f), idx(0), splatIdx(0)
        , splatsPerParticle(1), splatRadius(0.0f)
    {
        if (filter == MeshFilter::SedimentOnly) {
            splatsPerParticle = 9; // 1 center + 8 ring samples
            // Ring radius ~1.5 voxels: each particle becomes a ~3-voxel
            // diameter disk, large enough to fuse with adjacent halo
            // particles without bloating dense pile geometry.
            float dx = (float)geo.grid.dx;
            if (!(dx > 0)) dx = (float)geo.voxelSize;
            if (!(dx > 0)) dx = 0.025f;
            splatRadius = 1.5f * dx;
        }
    }

    void reset() override { idx = 0; splatIdx = 0; }

    /// Whether @p part should contribute samples for the current filter.
    bool accepts(const AreniteParticle& part) const {
        if (part.isEroded) return false;
        // Sediment-only meshing accepts *every* deposited particle (not
        // just those flagged isSurface).  The SPH-density surface test
        // mis-classifies sediment lying on top of dense sandstone --
        // the underlying rock inflates the local density past the
        // global "0.75 * maxDensity" cut-off, so genuine surface
        // sediment gets dropped.  Including all sediment also gives
        // Poisson a much denser input cloud.
        const bool requireSurface = (filter != MeshFilter::SedimentOnly);
        if (requireSurface && !part.isSurface) return false;
        if (filter == MeshFilter::SandstoneOnly && part.isSediment) return false;
        if (filter == MeshFilter::SedimentOnly && !part.isSediment) return false;
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

    // Count eligible particles (surface + alive + filter).  Must mirror
    // the gating in ArenitePointStream::read above: sediment-only
    // meshing skips the isSurface check so the count stays in sync.
    const bool requireSurface = (filter != MeshFilter::SedimentOnly);
    int eligibleCount = 0;
    for (exint i = 0; i < geo.particles.entries(); ++i) {
        const auto& p = geo.particles(i);
        if (p.isEroded) continue;
        if (requireSurface && !p.isSurface) continue;
        if (filter == MeshFilter::SandstoneOnly && p.isSediment) continue;
        if (filter == MeshFilter::SedimentOnly && !p.isSediment) continue;
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

    // Adaptive params for sparse input (e.g. scattered sediment).
    //
    // The ArenitePointStream constructor splats every sediment particle
    // into 9 oriented samples (1 center + 8 ring), so the cloud handed
    // to PoissonRecon is locally dense around each particle.  That
    // makes screened-Poisson defaults usable here without the surface
    // shattering into per-particle islands.  Two small biases remain:
    //
    //   * samplesPerNode = 3.0 (vs. default 1.5): mild noise smoothing
    //     so the splatted disk for one particle fuses with its
    //     neighbors instead of producing a faceted ring.
    //   * kernelDepth = depth - 3 (vs. default depth - 2): the density
    //     estimation kernel widens enough to bridge halo gaps that are
    //     larger than the splat-disk radius, keeping the reconstructed
    //     surface a single connected manifold rather than a collection
    //     of disconnected disks.
    //
    // pointWeight is left at the default (~2.0); a higher value makes
    // the iso-surface track every individual sample and re-fragments
    // the halo, which is exactly the failure mode we are trying to
    // avoid.
    if (filter == MeshFilter::SedimentOnly) {
        solverParams.samplesPerNode = 3.0f;
        const int wideKernel = (int)depth - 3;
        solverParams.kernelDepth = (unsigned int)(wideKernel < 0 ? 0 : wideKernel);
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

    // ── Clip the reconstructed mesh to the ground plane ─────────────────
    //
    // Screened Poisson always closes the implicit surface, even where the
    // input cloud has no samples.  Sediment particles all carry upward
    // normals, so the field has nothing to anchor it underneath the
    // cloud and the reconstructor extrapolates a large closure lobe
    // dangling below the ground.
    //
    // The clip rule is "keep a face iff all of its vertices are at or
    // above the ground plane".  This preserves the manifold property
    // produced by `forceManifold = true`: every edge of a kept face
    // has both vertices above the ground, so any face that originally
    // shared that edge is also kept (its two endpoints on the edge are
    // above ground, and a triangle has only three vertices).  A
    // centroid- or any-vertex-based rule could split adjacent faces
    // and introduce non-manifold seams along the cut.
    if (geo.useGroundPlane) {
        const float groundY = (float)geo.groundY;
        const float clipEps = 1e-4f;
        const size_t numVerts = vCoords.size() / 3;

        std::vector<std::vector<int>> kept;
        kept.reserve(polygons.size());
        for (const auto& face : polygons) {
            if (face.empty()) continue;
            bool allAbove = true;
            for (int vi : face) {
                if (vi < 0 || (size_t)vi >= numVerts ||
                    vCoords[3 * vi + 1] < groundY - clipEps) {
                    allAbove = false;
                    break;
                }
            }
            if (allAbove) kept.push_back(face);
        }
        polygons.swap(kept);
    }

    // ── Write into GU_Detail ────────────────────────────────────────────
    outputGeo->clearAndDestroy();

    size_t numVerts = vCoords.size() / 3;
    if (numVerts == 0 || polygons.empty()) return;

    // Compact: only emit vertices that surviving polygons actually use,
    // so ground-clipped orphans don't litter the output detail.
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
