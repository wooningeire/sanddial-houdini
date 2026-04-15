"""
Minimal test: draw a bright circle at the origin in the Scene Viewer.
Paste this into the Houdini Python Shell.
If you see a yellow ring at (0,0,0), GeometryDrawable works.
If you see nothing, there's a deeper Houdini viewport issue.
"""
import hou, math

# 1. Find the scene viewer
viewer = hou.ui.paneTabOfType(hou.paneTabType.SceneViewer)
if viewer is None:
    desktop = hou.ui.curDesktop()
    viewer = desktop.paneTabOfType(hou.paneTabType.SceneViewer)
print("viewer:", viewer)

# 2. Build a simple circle geometry
geo = hou.Geometry()
pts = []
for i in range(32):
    angle = 2.0 * math.pi * i / 32
    pt = geo.createPoint()
    # Make a big circle at y=0 with radius 2
    pt.setPosition(hou.Vector3(math.cos(angle) * 2.0, 0.0, math.sin(angle) * 2.0))
    pts.append(pt)
poly = geo.createPolygon()
for pt in pts:
    poly.addVertex(pt)
poly.setIsClosed(True)

# 3. Create drawable
d = hou.GeometryDrawable(viewer, hou.drawableGeometryType.Line, "test_ring_debug")
d.setGeometry(geo)
d.setParams({
    "color1": hou.Vector4(1.0, 1.0, 0.0, 1.0),
    "line_width": 3.0,
})
d.show(True)

# 4. Force redraw
vp = viewer.curViewport()
if vp:
    vp.draw()
    
print("Done. Look for a yellow ring at the origin (radius=2).")
print("If nothing visible, GeometryDrawable may require an active viewer state context.")
