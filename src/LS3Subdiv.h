#pragma once

class GU_Detail;

/// Least-Squares Subdivision Surfaces (LS3).
///
/// Applies iterative subdivision to a triangle mesh produced by the Poisson
/// reconstructor.  Each iteration performs:
///   1. Loop-style 1→4 triangle splitting (topological refinement).
///   2. Least-squares projection: for every vertex, fit a local bivariate
///      quadratic to the 1-ring neighbourhood and project the vertex onto
///      the fitted surface.
///
/// Reference: Boyé, Guennebaud & Schlick, "Least Squares Subdivision
/// Surfaces", Computer Graphics Forum 29(8), 2010.
class LS3Subdiv {
public:
    LS3Subdiv() = default;
    ~LS3Subdiv() = default;

    /// Apply @p iterations rounds of LS3 subdivision to the triangle mesh
    /// stored in @p geo.  Operates in-place.
    void subdivide(GU_Detail* geo, int iterations);

private:
    /// Loop-style 1→4 triangle split.  Creates edge midpoints (using the
    /// Loop averaging rule for interior edges) and replaces each triangle
    /// with 4 sub-triangles.
    void splitTriangles(GU_Detail* geo);

    /// For each vertex, gather the 1-ring, fit a local bivariate quadratic
    /// via least-squares, and project the vertex onto the fitted surface.
    void projectVertices(GU_Detail* geo);
};
