#include "LS3Subdiv.h"

#include <GU/GU_Detail.h>
#include <GU/GU_PrimPoly.h>
#include <GA/GA_Handle.h>
#include <GA/GA_Iterator.h>
#include <GA/GA_Primitive.h>
#include <GA/GA_OffsetList.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_Map.h>
#include <UT/UT_Array.h>
#include <SYS/SYS_Math.h>

#include <map>
#include <vector>
#include <set>
#include <cmath>

// ── Helpers ─────────────────────────────────────────────────────────────────

/// Canonical edge key (smaller offset first).
using EdgeKey = std::pair<GA_Offset, GA_Offset>;

static EdgeKey makeEdgeKey(GA_Offset a, GA_Offset b) {
    return (a < b) ? EdgeKey(a, b) : EdgeKey(b, a);
}

// ── subdivide ───────────────────────────────────────────────────────────────

void LS3Subdiv::subdivide(GU_Detail* geo, int iterations) {
    if (!geo || iterations <= 0) return;

    for (int i = 0; i < iterations; ++i) {
        splitTriangles(geo);
        projectVertices(geo);
    }
}

// ── splitTriangles (Loop-style 1→4) ─────────────────────────────────────────

void LS3Subdiv::splitTriangles(GU_Detail* geo) {
    // 1. Build adjacency: for each edge, which two primitives share it?
    //    Also record the "opposite" vertex in each triangle.
    struct EdgeInfo {
        GA_Offset midPt  = GA_INVALID_OFFSET;   // will be filled in
        GA_Offset tri[2] = { GA_INVALID_OFFSET, GA_INVALID_OFFSET };
        GA_Offset opp[2] = { GA_INVALID_OFFSET, GA_INVALID_OFFSET };
        int       count  = 0;
    };
    std::map<EdgeKey, EdgeInfo> edgeMap;

    // Collect all triangle edges and their adjacency.
    {
        GA_Offset primoff;
        GA_FOR_ALL_PRIMOFF(geo, primoff) {
            const GA_Primitive* prim = geo->getPrimitive(primoff);
            int nv = prim->getVertexCount();
            if (nv != 3) continue;   // skip non-triangles

            GA_Offset pts[3];
            for (int v = 0; v < 3; ++v)
                pts[v] = geo->vertexPoint(prim->getVertexOffset(v));

            for (int e = 0; e < 3; ++e) {
                GA_Offset a = pts[e];
                GA_Offset b = pts[(e + 1) % 3];
                GA_Offset c = pts[(e + 2) % 3]; // opposite vertex
                EdgeKey ek = makeEdgeKey(a, b);

                auto& info = edgeMap[ek];
                if (info.count < 2) {
                    info.tri[info.count] = primoff;
                    info.opp[info.count] = c;
                    info.count++;
                }
            }
        }
    }

    // 2. Create midpoint for every edge using the Loop rule.
    for (auto& [ek, info] : edgeMap) {
        UT_Vector3 p0 = geo->getPos3(ek.first);
        UT_Vector3 p1 = geo->getPos3(ek.second);

        UT_Vector3 midPos;
        if (info.count == 2) {
            // Interior edge: 3/8*(v0+v1) + 1/8*(opp_left + opp_right)
            UT_Vector3 pL = geo->getPos3(info.opp[0]);
            UT_Vector3 pR = geo->getPos3(info.opp[1]);
            midPos = (3.0 / 8.0) * (p0 + p1) + (1.0 / 8.0) * (pL + pR);
        } else {
            // Boundary edge: simple average
            midPos = 0.5 * (p0 + p1);
        }

        info.midPt = geo->appendPoint();
        geo->setPos3(info.midPt, midPos);
    }

    // 3. Replace each triangle with 4 sub-triangles.
    //
    //    Original triangle vertices:  A, B, C
    //    Edge midpoints:  mAB, mBC, mCA
    //
    //    Sub-triangles:
    //      (A,   mAB, mCA)
    //      (B,   mBC, mAB)
    //      (C,   mCA, mBC)
    //      (mAB, mBC, mCA)   ← center triangle
    //
    // Collect primitives to replace (can't mutate while iterating).
    UT_Array<GA_Offset> toRemove;
    {
        GA_Offset primoff;
        GA_FOR_ALL_PRIMOFF(geo, primoff) {
            const GA_Primitive* prim = geo->getPrimitive(primoff);
            if (prim->getVertexCount() != 3) continue;

            GA_Offset pts[3];
            for (int v = 0; v < 3; ++v)
                pts[v] = geo->vertexPoint(prim->getVertexOffset(v));

            // Look up midpoints.
            GA_Offset mid[3]; // mid[e] = midpoint of edge (pts[e], pts[(e+1)%3])
            for (int e = 0; e < 3; ++e) {
                EdgeKey ek = makeEdgeKey(pts[e], pts[(e + 1) % 3]);
                mid[e] = edgeMap[ek].midPt;
            }

            // Create 4 new triangles.
            auto makeTri = [&](GA_Offset a, GA_Offset b, GA_Offset c) {
                GU_PrimPoly* tri = GU_PrimPoly::build(geo, 3, GU_POLY_CLOSED, 0);
                tri->setVertexPoint(0, a);
                tri->setVertexPoint(1, b);
                tri->setVertexPoint(2, c);
            };

            makeTri(pts[0], mid[0], mid[2]);  // corner A
            makeTri(pts[1], mid[1], mid[0]);  // corner B
            makeTri(pts[2], mid[2], mid[1]);  // corner C
            makeTri(mid[0], mid[1], mid[2]);  // center

            toRemove.append(primoff);
        }
    }

    // Remove old triangles.
    for (exint i = toRemove.size() - 1; i >= 0; --i) {
        geo->destroyPrimitive(*geo->getPrimitive(toRemove(i)), true);
    }
}

// ── projectVertices (LS3 fitting) ───────────────────────────────────────────

void LS3Subdiv::projectVertices(GU_Detail* geo) {
    // 1. Build point-to-point adjacency (1-ring) from the current triangles.
    std::map<GA_Offset, std::set<GA_Offset>> adj;

    {
        GA_Offset primoff;
        GA_FOR_ALL_PRIMOFF(geo, primoff) {
            const GA_Primitive* prim = geo->getPrimitive(primoff);
            int nv = prim->getVertexCount();
            if (nv < 3) continue;

            GA_Offset pts[3];
            for (int v = 0; v < nv && v < 3; ++v)
                pts[v] = geo->vertexPoint(prim->getVertexOffset(v));

            for (int v = 0; v < 3; ++v) {
                adj[pts[v]].insert(pts[(v + 1) % 3]);
                adj[pts[v]].insert(pts[(v + 2) % 3]);
            }
        }
    }

    // 2. Compute face normals and accumulate per-vertex normals.
    std::map<GA_Offset, UT_Vector3> vertNormal;
    {
        GA_Offset primoff;
        GA_FOR_ALL_PRIMOFF(geo, primoff) {
            const GA_Primitive* prim = geo->getPrimitive(primoff);
            if (prim->getVertexCount() != 3) continue;

            GA_Offset pts[3];
            for (int v = 0; v < 3; ++v)
                pts[v] = geo->vertexPoint(prim->getVertexOffset(v));

            UT_Vector3 p0 = geo->getPos3(pts[0]);
            UT_Vector3 p1 = geo->getPos3(pts[1]);
            UT_Vector3 p2 = geo->getPos3(pts[2]);

            UT_Vector3 fn = cross(p1 - p0, p2 - p0);
            // Don't normalize yet — area-weighted accumulation is better.

            for (int v = 0; v < 3; ++v)
                vertNormal[pts[v]] += fn;
        }
    }

    // Normalize vertex normals.
    for (auto& [pt, n] : vertNormal) {
        fpreal len = n.length();
        if (len > 1e-12) n /= len;
        else n = UT_Vector3(0, 1, 0);
    }

    // 3. For each vertex, fit a local bivariate quadratic and project.
    //
    //    We fit:  w = a0 + a1*u + a2*v + a3*u^2 + a4*u*v + a5*v^2
    //
    //    in a local frame where the vertex is at the origin and the normal
    //    is the "up" direction.  The projection simply evaluates the fitted
    //    polynomial at (u,v) = (0,0), giving a height correction of a0.

    // Store new positions (don't write while iterating).
    std::map<GA_Offset, UT_Vector3> newPos;

    GA_Offset ptoff;
    GA_FOR_ALL_PTOFF(geo, ptoff) {
        auto ait = adj.find(ptoff);
        if (ait == adj.end() || ait->second.size() < 3) {
            // Not enough neighbors for a quadratic fit; keep position.
            continue;
        }

        const auto& ring = ait->second;
        UT_Vector3 P = geo->getPos3(ptoff);
        UT_Vector3 N = vertNormal[ptoff];

        // Build local tangent frame.
        UT_Vector3 T, B;
        {
            // Pick a vector not parallel to N.
            UT_Vector3 up(0, 1, 0);
            if (SYSabs(N.dot(up)) > 0.99) up = UT_Vector3(1, 0, 0);
            T = cross(N, up);
            T.normalize();
            B = cross(N, T);
            B.normalize();
        }

        // Gather 1-ring in local coordinates.
        int k = (int)ring.size();
        // We need at least 6 samples for 6 unknowns; include the vertex
        // itself as a sample at (0, 0, 0).
        int nSamples = k + 1;

        // Build the system A * x = b, where x = [a0 .. a5]^T.
        // A is (nSamples x 6),  b is (nSamples x 1).
        //
        // We solve the normal equations: (A^T A) x = A^T b.
        // A^T A is 6x6, A^T b is 6x1.

        double AtA[6][6] = {};
        double Atb[6]    = {};

        // Helper: add one sample (u, v, w) to the normal equations.
        auto addSample = [&](double u, double v, double w) {
            double row[6] = { 1.0, u, v, u*u, u*v, v*v };
            for (int i = 0; i < 6; ++i) {
                for (int j = 0; j < 6; ++j)
                    AtA[i][j] += row[i] * row[j];
                Atb[i] += row[i] * w;
            }
        };

        // The vertex itself: (0, 0, 0).
        addSample(0.0, 0.0, 0.0);

        // 1-ring neighbors.
        for (GA_Offset nb : ring) {
            UT_Vector3 d = geo->getPos3(nb) - P;
            double u = d.dot(T);
            double v = d.dot(B);
            double w = d.dot(N);
            addSample(u, v, w);
        }

        // Solve 6×6 system via Gaussian elimination with partial pivoting.
        // Augmented matrix: [AtA | Atb].
        double aug[6][7];
        for (int i = 0; i < 6; ++i) {
            for (int j = 0; j < 6; ++j)
                aug[i][j] = AtA[i][j];
            aug[i][6] = Atb[i];
        }

        bool singular = false;
        for (int col = 0; col < 6; ++col) {
            // Find pivot.
            int maxRow = col;
            double maxVal = std::abs(aug[col][col]);
            for (int row = col + 1; row < 6; ++row) {
                double v = std::abs(aug[row][col]);
                if (v > maxVal) { maxVal = v; maxRow = row; }
            }
            if (maxVal < 1e-14) { singular = true; break; }

            // Swap rows.
            if (maxRow != col) {
                for (int j = 0; j < 7; ++j)
                    std::swap(aug[col][j], aug[maxRow][j]);
            }

            // Eliminate below.
            for (int row = col + 1; row < 6; ++row) {
                double factor = aug[row][col] / aug[col][col];
                for (int j = col; j < 7; ++j)
                    aug[row][j] -= factor * aug[col][j];
            }
        }

        if (singular) continue; // skip this vertex

        // Back-substitution.
        double x[6] = {};
        for (int i = 5; i >= 0; --i) {
            double sum = aug[i][6];
            for (int j = i + 1; j < 6; ++j)
                sum -= aug[i][j] * x[j];
            x[i] = sum / aug[i][i];
        }

        // a0 is the height correction at (u,v) = (0,0).
        double a0 = x[0];

        // Clamp the correction to avoid wild extrapolation.
        // Use the average edge length as a scale reference.
        double avgEdge = 0.0;
        for (GA_Offset nb : ring)
            avgEdge += (geo->getPos3(nb) - P).length();
        avgEdge /= (double)k;

        double maxCorrection = 0.5 * avgEdge;
        if (std::abs(a0) > maxCorrection)
            a0 = (a0 > 0 ? 1.0 : -1.0) * maxCorrection;

        newPos[ptoff] = P + (fpreal)a0 * N;
    }

    // 4. Apply new positions.
    for (const auto& [pt, pos] : newPos) {
        geo->setPos3(pt, pos);
    }
}
