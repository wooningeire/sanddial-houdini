#include "PoissonMesher.h"

// PoissonRecon headers — order matters
#include "PreProcessor.h"
#include "Reconstructors.h"

#include <GU/GU_Detail.h>
#include <GA/GA_Handle.h>
#include <GU/GU_PrimPoly.h>
#include <vector>

using namespace PoissonRecon;

// ── Input stream: feeds Sanddial particles as oriented samples ──────────────
struct ArenitePointStream
    : public Reconstructor::InputOrientedSampleStream<float, 3>
{
    const AreniteGeometry& geo;
    MeshFilter filter;
    exint idx;

    ArenitePointStream(const AreniteGeometry& g, MeshFilter f)
        : geo(g), filter(f), idx(0) {}

    void reset() override { idx = 0; }

    bool read(Point<float, 3>& p, Point<float, 3>& n) override {
        while (idx < geo.particles.entries()) {
            const auto& part = geo.particles(idx);
            ++idx;
            if (part.isEroded) continue;
            if (!part.isSurface) continue;
            if (filter == MeshFilter::SandstoneOnly && part.isSediment) continue;
            if (filter == MeshFilter::SedimentOnly && !part.isSediment) continue;

            p[0] = (float)part.position.x();
            p[1] = (float)part.position.y();
            p[2] = (float)part.position.z();
            n[0] = (float)part.normal.x();
            n[1] = (float)part.normal.y();
            n[2] = (float)part.normal.z();
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

    // Count eligible particles (surface + alive + filter)
    int eligibleCount = 0;
    for (exint i = 0; i < geo.particles.entries(); ++i) {
        const auto& p = geo.particles(i);
        if (p.isEroded || !p.isSurface) continue;
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
    solverParams.depth = (unsigned int)depth;
    solverParams.scale = scale;
    solverParams.verbose = false;

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

    // ── Write into GU_Detail ────────────────────────────────────────────
    outputGeo->clearAndDestroy();

    size_t numVerts = vCoords.size() / 3;
    if (numVerts == 0) return;

    // Create points
    GA_Offset startPt = outputGeo->appendPointBlock(numVerts);
    GA_RWHandleV3 posH(outputGeo->getP());
    for (size_t i = 0; i < numVerts; ++i) {
        GA_Offset pt = startPt + (GA_Offset)i;
        posH.set(pt, UT_Vector3(vCoords[3*i], vCoords[3*i+1], vCoords[3*i+2]));
    }

    // Create polygons
    for (const auto& face : polygons) {
        GU_PrimPoly* prim = GU_PrimPoly::build(outputGeo, (int)face.size(),
                                                GU_POLY_CLOSED, 0);
        for (size_t v = 0; v < face.size(); ++v) {
            prim->setVertexPoint((int)v, startPt + (GA_Offset)face[v]);
        }
    }
}
