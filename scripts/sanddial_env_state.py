"""
Sanddial - Environment Edit viewer state.

Provides viewport handles for editing wind and precipitation parameters directly
in the 3D view without having to use the Parameter Pane sliders.

Controls
--------
LMB drag           : rotate wind direction (yaw = left/right, pitch = up/down)
Shift + LMB drag   : adjust wind speed  (drag right = faster)
Ctrl  + LMB drag   : adjust precipitation (drag right = more rain)
Scroll wheel       : zoom wind-arrow scale (visual only)

A coloured arrow in the viewport shows the current wind direction, scaled by
wind speed.  A small vertical bar shows the precipitation level.

The state is activated when viewport_mode == 2 ("Environment Edit").  The
startup script (sanddial_startup.py) handles the mode-switch callback that
enters / exits this state.
"""

import hou
import math
import viewerstate.utils as su


# ── Geometry helpers ────────────────────────────────────────────────────────

def _make_arrow_geo():
    """Return a hou.Geometry line-art arrow pointing along +X (length 1)."""
    geo = hou.Geometry()

    def pt(x, y, z):
        p = geo.createPoint()
        p.setPosition(hou.Vector3(x, y, z))
        return p

    def line(a, b):
        poly = geo.createPolygon(is_closed=False)
        poly.addVertex(a)
        poly.addVertex(b)

    # Shaft
    o  = pt(0.00,  0.00,  0.00)
    t  = pt(1.00,  0.00,  0.00)
    line(o, t)

    # Arrowhead – four fins so the arrow reads well from any angle
    for dy, dz in ((0.18, 0.0), (-0.18, 0.0), (0.0, 0.18), (0.0, -0.18)):
        fin = pt(0.72, dy, dz)
        line(fin, t)

    return geo


def _make_precip_bar_geo():
    """Return a thin vertical bar used to visualise precipitation level."""
    geo = hou.Geometry()

    origin = geo.createPoint()
    origin.setPosition(hou.Vector3(0, 0, 0))
    top = geo.createPoint()
    top.setPosition(hou.Vector3(0, 1, 0))

    poly = geo.createPolygon(is_closed=False)
    poly.addVertex(origin)
    poly.addVertex(top)

    return geo


def _build_arrow_xform(wind_dir_tuple, wind_speed):
    """
    Build a hou.Matrix4 that rotates the +X arrow to lie along wind_dir and
    scales it by wind_speed (clamped for viewport readability).
    """
    wx, wy, wz = wind_dir_tuple
    d = hou.Vector3(wx, wy, wz)
    if d.length() < 1e-5:
        d = hou.Vector3(1.0, 0.0, 0.0)
    d = d.normalized()

    # Visual scale: map wind speed 0-20 → arrow length 0.5-3.5
    scale = 0.5 + min(wind_speed / 20.0, 1.0) * 3.0

    # Build rotation matrix from +X to d.
    x_axis = hou.Vector3(1, 0, 0)
    dot = x_axis.dot(d)
    cross = x_axis.cross(d)

    m = hou.Matrix4(1)
    if cross.length() < 1e-6:
        if dot < 0:
            # Anti-parallel: 180° about Y
            m = hou.Matrix4((-1, 0, 0, 0,
                              0, 1, 0, 0,
                              0, 0,-1, 0,
                              0, 0, 0, 1))
        # else m stays identity (parallel)
    else:
        angle = math.degrees(math.acos(max(-1.0, min(1.0, dot))))
        axis  = cross.normalized()
        m = hou.Matrix4(1)
        m.setToRotateByAxis(axis, angle)

    # Scale then rotate: build scale matrix and multiply.
    s = hou.Matrix4(1)
    s.setAt(0, 0, scale)
    s.setAt(1, 1, scale)
    s.setAt(2, 2, scale)

    out = s * m   # Houdini: row × matrix, so s first then rotation

    # Place the arrow 1.5 units up in Y so it floats above the geometry.
    out.setAt(3, 1, 1.5)
    return out


def _build_precip_xform(precipitation):
    """Scale the precipitation bar by the current precipitation value."""
    height = max(0.05, min(precipitation * 0.5, 3.0))
    m = hou.Matrix4(1)
    m.setAt(1, 1, height)
    # Position it 2 units to the right of the arrow origin
    m.setAt(3, 0, 2.0)
    m.setAt(3, 1, 0.0)
    return m


# ── Viewer state ────────────────────────────────────────────────────────────

class State(object):
    MSG = ("LMB drag: wind dir  |  "
           "Shift+drag: wind speed  |  "
           "Ctrl+drag: precipitation")

    def __init__(self, state_name, scene_viewer):
        self._scene_viewer = scene_viewer

        # Drag bookkeeping
        self._dragging        = False
        self._drag_mode       = "dir"   # "dir" | "speed" | "precip"
        self._drag_start_xy   = (0, 0)
        self._drag_start_dir  = (1.0, 0.0, 0.0)
        self._drag_start_spd  = 5.0
        self._drag_start_pre  = 1.0

        # Wind-arrow drawable
        self._arrow_geo = _make_arrow_geo()
        self._arrow_draw = hou.GeometryDrawable(
            scene_viewer,
            hou.drawableGeometryType.Line,
            "sanddial_wind_arrow",
        )
        self._arrow_draw.setGeometry(self._arrow_geo)
        self._arrow_draw.setParams({
            "color1":     hou.Vector4(0.3, 0.75, 1.0, 1.0),
            "line_width": 3.0,
        })

        # Precipitation bar drawable
        self._precip_geo = _make_precip_bar_geo()
        self._precip_draw = hou.GeometryDrawable(
            scene_viewer,
            hou.drawableGeometryType.Line,
            "sanddial_precip_bar",
        )
        self._precip_draw.setGeometry(self._precip_geo)
        self._precip_draw.setParams({
            "color1":     hou.Vector4(0.2, 0.45, 1.0, 0.85),
            "line_width": 6.0,
        })

    # ── Lifecycle ──────────────────────────────────────────────────────────

    def onEnter(self, kwargs):
        self._scene_viewer.setPromptMessage(self.MSG)
        self._arrow_draw.show(True)
        self._precip_draw.show(True)
        self._sync_drawables(kwargs["node"])

    def onExit(self, kwargs):
        self._arrow_draw.show(False)
        self._precip_draw.show(False)

    def onResume(self, kwargs):
        self._scene_viewer.setPromptMessage(self.MSG)
        self._arrow_draw.show(True)
        self._precip_draw.show(True)
        self._sync_drawables(kwargs["node"])

    def onInterrupt(self, kwargs):
        self._arrow_draw.show(False)
        self._precip_draw.show(False)

    def onDraw(self, kwargs):
        handle = kwargs["draw_handle"]
        self._arrow_draw.draw(handle)
        self._precip_draw.draw(handle)

    # ── Mouse handling ─────────────────────────────────────────────────────

    def onMouseEvent(self, kwargs):
        node   = kwargs["node"]
        ui_evt = kwargs["ui_event"]
        reason = ui_evt.reason()
        device = ui_evt.device()

        mx, my = device.mouseX(), device.mouseY()
        shift  = device.isShiftKey()
        ctrl   = device.isCtrlKey()

        if reason == hou.uiEventReason.Start:
            self._dragging       = True
            self._drag_start_xy  = (mx, my)
            self._drag_start_dir = (
                node.evalParm("wind_directionx"),
                node.evalParm("wind_directiony"),
                node.evalParm("wind_directionz"),
            )
            self._drag_start_spd = node.evalParm("wind_speed")
            self._drag_start_pre = node.evalParm("precipitation")

            if shift:
                self._drag_mode = "speed"
            elif ctrl:
                self._drag_mode = "precip"
            else:
                self._drag_mode = "dir"

        elif reason == hou.uiEventReason.Active and self._dragging:
            dx = mx - self._drag_start_xy[0]
            dy = my - self._drag_start_xy[1]
            self._apply_drag(node, dx, dy)
            self._sync_drawables(node)

        elif reason == hou.uiEventReason.Changed:
            self._dragging = False

        return False   # don't consume: allow native camera controls

    # ── Drag application ───────────────────────────────────────────────────

    def _apply_drag(self, node, dx, dy):
        if self._drag_mode == "speed":
            new_spd = max(0.0, self._drag_start_spd + dx * 0.05)
            node.parm("wind_speed").set(new_spd)

        elif self._drag_mode == "precip":
            new_pre = max(0.0, self._drag_start_pre + dx * 0.02)
            node.parm("precipitation").set(new_pre)

        else:  # "dir"
            wx, wy, wz = self._drag_start_dir
            base = hou.Vector3(wx, wy, wz)
            if base.length() < 1e-5:
                base = hou.Vector3(1, 0, 0)
            base = base.normalized()

            # Yaw (horizontal drag → rotate around world Y)
            yaw = math.radians(dx * 0.4)
            cy, sy = math.cos(yaw), math.sin(yaw)
            nx =  base[0] * cy + base[2] * sy
            ny =  base[1]
            nz = -base[0] * sy + base[2] * cy

            # Pitch (vertical drag → rotate around the right axis)
            pitch = math.radians(-dy * 0.4)
            fwd   = hou.Vector3(nx, ny, nz)
            right = fwd.cross(hou.Vector3(0, 1, 0))
            if right.length() < 1e-5:
                right = hou.Vector3(0, 0, 1)
            right = right.normalized()

            cp, sp = math.cos(pitch), math.sin(pitch)
            fx = nx * cp + right[0] * sp
            fy = ny * cp + right[1] * sp
            fz = nz * cp + right[2] * sp

            node.parmTuple("wind_direction").set((fx, fy, fz))

    # ── Drawable sync ──────────────────────────────────────────────────────

    def _sync_drawables(self, node):
        wd = (
            node.evalParm("wind_directionx"),
            node.evalParm("wind_directiony"),
            node.evalParm("wind_directionz"),
        )
        ws  = node.evalParm("wind_speed")
        pre = node.evalParm("precipitation")

        self._arrow_draw.setTransform(_build_arrow_xform(wd, ws))
        self._precip_draw.setTransform(_build_precip_xform(pre))


# ── Registration ─────────────────────────────────────────────────────────────

def createViewerStateTemplate():
    tpl = hou.ViewerStateTemplate(
        "sop_sanddial_environment_edit",
        "Sanddial Environment Edit",
        hou.sopNodeTypeCategory(),
    )
    tpl.bindFactory(State)
    tpl.bindIcon("SOP_sanddial")
    return tpl
