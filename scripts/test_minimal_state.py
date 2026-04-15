"""
Minimal test v2: call show(True) in __init__ instead of onEnter.
"""
import hou, math

class MinimalState2(object):
    def __init__(self, state_name, scene_viewer):
        self.state_name = state_name
        self.scene_viewer = scene_viewer
        
        geo = hou.Geometry()
        pts = []
        for i in range(32):
            a = 2.0 * math.pi * i / 32
            pt = geo.createPoint()
            pt.setPosition(hou.Vector3(math.cos(a) * 3.0, 0.0, math.sin(a) * 3.0))
            pts.append(pt)
        poly = geo.createPolygon()
        for pt in pts:
            poly.addVertex(pt)
        poly.setIsClosed(True)
        
        self._d = hou.GeometryDrawable(
            scene_viewer, hou.drawableGeometryType.Line, "test_circle_v2"
        )
        self._d.setGeometry(geo)
        self._d.setParams({
            "color1": hou.Vector4(1.0, 1.0, 0.0, 1.0),
            "line_width": 4.0,
        })
        # Show it immediately in __init__ instead of onEnter!
        self._d.show(True)
        print("MINIMAL2: __init__ done, show(True) called")

    def onEnter(self, kwargs):
        print("MINIMAL2: onEnter called!")

    def onExit(self, kwargs):
        self._d.show(False)
        print("MINIMAL2: onExit")

    def onDraw(self, kwargs):
        self._d.draw(kwargs["draw_handle"])
        # One-time debug print
        if not hasattr(self, '_draw_logged'):
            self._draw_logged = True
            print("MINIMAL2: onDraw called!")

    def onMouseEvent(self, kwargs):
        return True

_tpl = hou.ViewerStateTemplate("minimal_test_v2", "Minimal Test v2", hou.sopNodeTypeCategory())
_tpl.bindFactory(MinimalState2)
_tpl.bindIcon("SOP_scatter")

try:
    hou.ui.unregisterViewerState("minimal_test_v2")
except:
    pass

hou.ui.registerViewerState(_tpl)

import toolutils
_v = toolutils.sceneViewer()
if _v is None:
    _v = hou.ui.paneTabOfType(hou.paneTabType.SceneViewer)
if _v:
    _v.setCurrentState("minimal_test_v2")
    print("Entered minimal_test_v2")
