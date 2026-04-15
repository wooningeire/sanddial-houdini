"""
Sanddial Erodibility Paint Viewer State
----------------------------------------
Standalone viewer state (NOT embedded in HDA's ViewerStateModule).
Registered with a unique name so it won't conflict with the HDA's built-in state.
"""

import hou
import math

STATE_NAME = "sop_sanddial_erodibility_paint"


def _make_circle_geo(segments=64):
    """Create a hou.Geometry containing a unit-radius circle (line loop)."""
    geo = hou.Geometry()
    pts = []
    for i in range(segments):
        angle = 2.0 * math.pi * i / segments
        pt = geo.createPoint()
        pt.setPosition(hou.Vector3(math.cos(angle), 0.0, math.sin(angle)))
        pts.append(pt)

    poly = geo.createPolygon()
    for pt in pts:
        poly.addVertex(pt)
    poly.setIsClosed(True)
    return geo


class State(object):
    """Sanddial viewer state for erodibility painting."""

    MSG = "LMB: Paint erodibility  |  Scroll: Brush size  |  Shift+Scroll: Strength"

    def __init__(self, state_name, scene_viewer):
        self.state_name = state_name
        self.scene_viewer = scene_viewer

        # Brush drawable
        self._brush_geo = _make_circle_geo()
        self._brush_drawable = hou.GeometryDrawable(
            scene_viewer,
            hou.drawableGeometryType.Line,
            "sanddial_brush_ring",
        )
        self._brush_drawable.setGeometry(self._brush_geo)
        self._brush_drawable.setParams({
            "color1": hou.Vector4(1.0, 0.85, 0.2, 1.0),
            "line_width": 3.0,
        })
        # Show immediately — onEnter is never called in this Houdini context
        self._brush_drawable.show(True)

        self._brush_pos = hou.Vector3(0, 0, 0)
        self._brush_normal = hou.Vector3(0, 1, 0)
        self._is_painting = False
        self._debug_logged = False

        # Find the sanddial node immediately
        self._node = None
        for n in hou.node('/').allSubChildren():
            if n.parm('viewport_mode') is not None:
                self._node = n
                break
        
        scene_viewer.setPromptMessage(self.MSG)
        print("Sanddial Paint: __init__ complete, node =", self._node)

    # ── Lifecycle ────────────────────────────────────────────────────────

    def onEnter(self, kwargs):
        self._node = kwargs.get("node", None) or self._node
        self._brush_drawable.show(True)
        self.scene_viewer.setPromptMessage(self.MSG)
        print("Sanddial Paint: onEnter")

    def onExit(self, kwargs):
        self._brush_drawable.show(False)
        print("Sanddial Paint: onExit")

    def onResume(self, kwargs):
        self._brush_drawable.show(True)

    def onInterrupt(self, kwargs):
        self._brush_drawable.show(False)

    # ── Drawing ──────────────────────────────────────────────────────────

    def onDraw(self, kwargs):
        try:
            handle = kwargs["draw_handle"]
            self._brush_drawable.draw(handle)
        except Exception as e:
            print("Sanddial Paint onDraw error:", e)

    # ── Mouse ────────────────────────────────────────────────────────────

    def onMouseEvent(self, kwargs):
        try:
            ui_event = kwargs["ui_event"]
            device = ui_event.device()
            node = self._node

            # Get the mouse ray
            ray_origin, ray_dir = ui_event.ray()

            # Get geometry - try the node and its display child
            geo = None
            if node:
                geo = node.geometry()
                if geo is None or geo.intrinsicValue("pointcount") == 0:
                    # Try display node or children
                    try:
                        disp = node.displayNode()
                        if disp:
                            geo = disp.geometry()
                    except:
                        pass
                    if geo is None or geo.intrinsicValue("pointcount") == 0:
                        for child in node.children():
                            try:
                                cg = child.geometry()
                                if cg and cg.intrinsicValue("pointcount") > 0:
                                    geo = cg
                                    break
                            except:
                                pass

            hit = False

            if geo is not None and geo.intrinsicValue("pointcount") > 0:
                bbox = geo.boundingBox()
                center = bbox.center()

                # Intersect ray with horizontal plane at y = center.y
                if abs(ray_dir[1]) > 1e-6:
                    t = (center[1] - ray_origin[1]) / ray_dir[1]
                    if t > 0:
                        hit_pos = hou.Vector3(
                            ray_origin[0] + ray_dir[0] * t,
                            ray_origin[1] + ray_dir[1] * t,
                            ray_origin[2] + ray_dir[2] * t,
                        )
                        # Find nearest geometry point
                        nearest_pt = geo.nearestPoint(hit_pos)
                        if nearest_pt is not None:
                            self._brush_pos = nearest_pt.position()
                            self._brush_normal = hou.Vector3(0, 1, 0)
                            hit = True

                if not hit:
                    self._brush_pos = center
                    self._brush_normal = hou.Vector3(0, 1, 0)
                    hit = True

            # Update drawable transform
            radius = 0.5
            if node:
                try:
                    radius = float(node.evalParm("brush_radius"))
                except:
                    radius = 0.5

            self._update_brush_xform(radius)

            # ── Painting ─────────────────────────────────────────────
            is_lmb = device.isLeftButton()

            if is_lmb and hit and not self._is_painting:
                self._is_painting = True
                self.scene_viewer.beginStateUndo("Paint Erodibility")

            if self._is_painting and hit:
                self._apply_brush(node)

            if not is_lmb and self._is_painting:
                self._is_painting = False
                self.scene_viewer.endStateUndo()

            return True

        except Exception as e:
            print("Sanddial Paint onMouseEvent error:", e)
            import traceback
            traceback.print_exc()
            return False

    # ── Scroll ───────────────────────────────────────────────────────────

    def onMouseWheelEvent(self, kwargs):
        try:
            ui_event = kwargs["ui_event"]
            device = ui_event.device()
            node = self._node
            if not node:
                return False

            scroll = device.mouseWheel()
            if scroll == 0:
                return False

            if device.isShiftKey():
                cur = node.evalParm("brush_strength")
                step = 0.02 * (1 if scroll > 0 else -1)
                node.parm("brush_strength").set(max(0.01, cur + step))
            else:
                cur = node.evalParm("brush_radius")
                factor = 1.15 if scroll > 0 else (1.0 / 1.15)
                node.parm("brush_radius").set(max(0.01, cur * factor))

            self._update_brush_xform(node.evalParm("brush_radius"))
            return True
        except Exception as e:
            print("Sanddial Paint onMouseWheelEvent error:", e)
            return False

    # ── Helpers ──────────────────────────────────────────────────────────

    def _update_brush_xform(self, radius):
        """Position and scale the drawable ring at the brush location."""
        up = self._brush_normal.normalized()
        ref = hou.Vector3(0, 0, 1) if abs(up[1]) < 0.99 else hou.Vector3(1, 0, 0)
        right = up.cross(ref).normalized()
        fwd = right.cross(up).normalized()

        m = hou.Matrix4((
            right[0] * radius, right[1] * radius, right[2] * radius, 0,
            up[0] * radius,    up[1] * radius,    up[2] * radius,    0,
            fwd[0] * radius,   fwd[1] * radius,   fwd[2] * radius,  0,
            self._brush_pos[0], self._brush_pos[1], self._brush_pos[2], 1,
        ))
        self._brush_drawable.setTransform(m)

    def _apply_brush(self, node):
        """Write brush parameters to the node so the C++ cook applies them."""
        if not node:
            return
        try:
            node.parmTuple("brush_pos").set(
                (self._brush_pos[0], self._brush_pos[1], self._brush_pos[2])
            )
            node.parm("brush_active").set(1)
        except Exception as e:
            print("Sanddial Paint _apply_brush error:", e)


def createViewerStateTemplate():
    """Create and return the viewer state template."""
    template = hou.ViewerStateTemplate(
        STATE_NAME, "Erodibility Paint", hou.sopNodeTypeCategory()
    )
    template.bindFactory(State)
    template.bindIcon("SOP_paint")
    return template


def register():
    """Register this viewer state. Call from startup or manually."""
    try:
        hou.ui.unregisterViewerState(STATE_NAME)
    except:
        pass
    tpl = createViewerStateTemplate()
    hou.ui.registerViewerState(tpl)
    print(f"Sanddial: Registered viewer state '{STATE_NAME}'")


def enter(viewer=None):
    """Force the Scene Viewer into this state."""
    if viewer is None:
        import toolutils
        viewer = toolutils.sceneViewer()
    if viewer is None:
        viewer = hou.ui.paneTabOfType(hou.paneTabType.SceneViewer)
    if viewer:
        viewer.setCurrentState(STATE_NAME)
        print(f"Sanddial: Entered state '{STATE_NAME}'")


# Auto-register when this module is loaded
register()
