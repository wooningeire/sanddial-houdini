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

        # Monotonically-increasing stroke ID written to brush_stroke_id.
        # Each call ultimately produces a stroke even if mouse events arrive
        # between SOP cooks, because every set() value is unique.  Falls
        # back to toggling brush_active on older C++ builds.
        self._stroke_counter = 0

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
        if self._node:
            # Auto-switch to erodibility visualization on state entry.
            try:
                self._node.parm("visualize_mode").set(1)
            except Exception:
                pass

            # Strip any stale keyframes/animation on the brush-trigger parms
            # so subsequent Python .set() calls write plain values rather
            # than creating new keyframes.
            for pname in ("brush_active", "brush_stroke_id",
                          "brush_posx", "brush_posy", "brush_posz"):
                try:
                    p = self._node.parm(pname)
                    if p is not None:
                        p.deleteAllKeyframes()
                except Exception:
                    pass

            # Re-sync the stroke counter to whatever value is currently in
            # the parm, so the very first _apply_brush call after re-entry
            # produces a value the C++ side has not yet seen.
            try:
                cur = int(self._node.evalParm("brush_stroke_id") or 0)
                self._stroke_counter = cur
            except Exception:
                pass

            # Loud diagnostic: if the C++ HDA hasn't been rebuilt with the
            # new trigger parm, drag painting will silently use the lossy
            # toggle fallback.  Surface this so it isn't a mystery.
            if self._node.parm("brush_stroke_id") is None:
                print("Sanddial Paint WARNING: 'brush_stroke_id' parm is "
                      "missing on the node.  This means the C++ HDA is "
                      "from an older build.  Rebuild the project for "
                      "reliable drag painting; otherwise the state will "
                      "fall back to the brush_active toggle which loses "
                      "strokes when mouse events outpace SOP cooks.")
        print("Sanddial Paint: onEnter")

    def onExit(self, kwargs):
        try:
            self._brush_drawable.show(False)
        except Exception:
            pass
        try:
            node = self._node
            if node:
                node.parm("viewport_mode").set(0)
        except Exception:
            pass
        print("Sanddial Paint: onExit")

    def onResume(self, kwargs):
        try:
            self._brush_drawable.show(True)
        except Exception:
            pass

    def onInterrupt(self, kwargs):
        try:
            self._brush_drawable.show(False)
        except Exception:
            pass

    def onMenuAction(self, kwargs):
        """Handle menu item selection."""
        menu_item = kwargs["menu_item"]
        if menu_item.startswith("visualize_"):
            try:
                mode_str = menu_item.split("_")[1]
                mode_map = {
                    "nothing": 0,
                    "erodibility": 1,
                    "viability": 2,
                    "stress": 3,
                    "normals": 4,
                    "deflation": 5,
                    "abrasion": 6,
                    "water": 7,
                    "total": 8
                }
                mode = mode_map.get(mode_str, 0)
                if self._node:
                    self._node.parm("visualize_mode").set(mode)
            except Exception as e:
                print("Sanddial Paint Menu error:", e)

    # ── Drawing ──────────────────────────────────────────────────────────

    def onDraw(self, kwargs):
        try:
            handle = kwargs["draw_handle"]
            self._brush_drawable.draw(handle)
        except Exception as e:
            print("Sanddial Paint onDraw error:", e)

    # ── Keyboard ─────────────────────────────────────────────────────────

    def onKeyEvent(self, kwargs):
        """Handle Escape to exit paint mode and return to View."""
        try:
            ui_event = kwargs["ui_event"]
            key = ui_event.device().keyString()
            if key == "Esc":
                import sanddial_startup as _sd
                node_path = self._node.path() if self._node else ""
                _sd._enter_state_from_button(node_path, 0)
                return True
        except Exception:
            pass
        return False

    # ── Mouse ────────────────────────────────────────────────────────────

    def onMouseEvent(self, kwargs):
        try:
            ui_event = kwargs["ui_event"]
            device = ui_event.device()
            node = self._node

            # Get the mouse ray
            ray_origin, ray_dir = ui_event.ray()

            # node.geometry() returns the SOP's last cooked geometry and, if
            # the node is dirty, force-completes the pending cook before
            # returning.  We rely on this implicit per-event cook to keep
            # painting reliable: the previous event's brush parm changes are
            # committed here before we read the geometry for sphere tracing
            # or write the next stroke's parms.
            geo = None
            if node:
                geo = node.geometry()
                if geo is None or geo.intrinsicValue("pointcount") == 0:
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
                bbox_center = bbox.center()
                bbox_diag = bbox.sizevec().length()

                ray_origin_v = hou.Vector3(ray_origin[0], ray_origin[1], ray_origin[2])
                ray_dir_v = hou.Vector3(ray_dir[0], ray_dir[1], ray_dir[2]).normalized()

                # Inter-particle spacing controls hit tolerance.  voxel_size
                # on the node mirrors the simulation grid spacing, which is
                # also the natural particle spacing.
                voxel = 0.2
                if node:
                    try:
                        voxel = float(node.evalParm("voxel_size"))
                    except Exception:
                        pass
                tol      = max(voxel * 2.0, 0.01)
                min_step = max(voxel * 0.25, 0.005)

                # Restrict the walk to the segment of the ray that overlaps
                # the geometry's bounding sphere (cheap superset of the bbox).
                to_center = bbox_center - ray_origin_v
                t_center  = to_center.dot(ray_dir_v)
                t_min = max(0.0, t_center - bbox_diag)
                t_max = t_center + bbox_diag

                # Sphere-trace the ray against the point cloud.  At each
                # sample, nearestPoint() gives the distance `d` to the
                # closest particle; by the triangle inequality no particle
                # can come within `tol` of the ray for the next `d - tol`
                # units, so we can safely skip that whole interval.
                hit_pos = None
                t = t_min
                for _ in range(32):  # safety bound; typical convergence < 10
                    if t > t_max:
                        break
                    sample = ray_origin_v + ray_dir_v * t
                    nearest_pt = geo.nearestPoint(sample)
                    if nearest_pt is None:
                        break
                    np_pos = nearest_pt.position()
                    d = (np_pos - sample).length()
                    if d < tol:
                        # `sample` is up to `tol` *in front of* the surface
                        # along the ray.  The C++ brush is a 3D sphere; an
                        # offset of T between its centre and the surface
                        # shrinks the lateral disc on the surface from R
                        # to sqrt(R²-T²), which makes the painted footprint
                        # noticeably smaller than the on-screen brush ring.
                        # Project the nearest particle onto the ray and use
                        # that depth as the brush centre instead -- still on
                        # the ray (so drag stays smooth and continuous), but
                        # now at the actual surface depth so the sphere cuts
                        # a full-radius disc on the surface.
                        t_surface = (np_pos - ray_origin_v).dot(ray_dir_v)
                        if t_surface > 0:
                            hit_pos = ray_origin_v + ray_dir_v * t_surface
                        else:
                            hit_pos = sample
                        break
                    t += max(d - tol, min_step)

                if hit_pos is not None:
                    self._brush_pos = hit_pos
                    hit = True
                else:
                    # Off-surface hover: fall back to a view-aligned plane
                    # through the bbox center so the brush ring still tracks
                    # the cursor.  Painting is gated on `hit`, so nothing is
                    # painted while the brush is in empty space.
                    vp = self.scene_viewer.curViewport()
                    if vp:
                        view_xform = vp.viewTransform()
                        plane_normal = hou.Vector3(
                            -view_xform.at(2, 0),
                            -view_xform.at(2, 1),
                            -view_xform.at(2, 2),
                        ).normalized()
                    else:
                        plane_normal = hou.Vector3(0, 0, -1)

                    denom = plane_normal.dot(ray_dir_v)
                    if abs(denom) > 1e-8:
                        t_plane = plane_normal.dot(bbox_center - ray_origin_v) / denom
                        if t_plane > 0:
                            self._brush_pos = ray_origin_v + ray_dir_v * t_plane

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

            if self._is_painting and hit and node:
                self._apply_brush(node, self._brush_pos)

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
        """Position and scale the drawable ring to face the camera.
        
        _make_circle_geo() builds the ring in the XZ plane (all Y=0),
        so its natural normal is +Y.  We need to rotate so that +Y maps
        to the toward-camera direction (viewTransform row 2).
        """
        vp = self.scene_viewer.curViewport()
        if vp:
            view_xform = vp.viewTransform()
            # Row 2 of the view transform is the camera's local +Z in world
            # space, which points FROM the scene TOWARD the camera.
            cam_toward = hou.Vector3(
                view_xform.at(2, 0),
                view_xform.at(2, 1),
                view_xform.at(2, 2),
            ).normalized()
        else:
            cam_toward = hou.Vector3(0, 1, 0)

        # We want the ring's +Y (its normal) to point toward the camera.
        # Pick a stable "up" reference that isn't parallel to cam_toward.
        ref   = hou.Vector3(0, 0, 1) if abs(cam_toward[1]) > 0.9 else hou.Vector3(0, 1, 0)
        right = cam_toward.cross(ref).normalized()   # ring's +X
        fwd   = right.cross(cam_toward).normalized() # ring's +Z

        # Matrix rows: X=right, Y=cam_toward (ring normal), Z=fwd
        m = hou.Matrix4((
            right[0]      * radius, right[1]      * radius, right[2]      * radius, 0,
            cam_toward[0] * radius, cam_toward[1] * radius, cam_toward[2] * radius, 0,
            fwd[0]        * radius, fwd[1]        * radius, fwd[2]        * radius, 0,
            self._brush_pos[0],     self._brush_pos[1],     self._brush_pos[2],     1,
        ))
        self._brush_drawable.setTransform(m)

    def _apply_brush(self, node, paint_pos=None):
        """Write brush parameters to the node so the C++ cook applies them.

        Preferred trigger: `brush_stroke_id` (PRM_INT, hidden) -- a
        monotonically-increasing counter.  C++ fires a stroke whenever the
        value differs from the last-seen value, so each call is guaranteed
        to produce a stroke even if many mouse events arrive between SOP
        cooks.

        Fallback for older C++ builds that don't expose brush_stroke_id:
        toggle the public `brush_active` 0/1 parm.  This still has the
        cancellation race (0->1->0 between cooks drops the stroke), but
        is better than no trigger at all.  We log a one-time warning so
        the user knows to rebuild for reliable drag painting.
        """
        if not node:
            return
        pos = paint_pos if paint_pos is not None else self._brush_pos

        # 1. Brush position is needed by either trigger path.
        try:
            node.parmTuple("brush_pos").set((pos[0], pos[1], pos[2]))
        except Exception as e:
            print("Sanddial Paint: brush_pos set failed:", e)
            return

        self._stroke_counter += 1

        # 2. Preferred path: monotonic counter.
        stroke_parm = node.parm("brush_stroke_id")
        if stroke_parm is not None:
            try:
                stroke_parm.set(self._stroke_counter)
                return
            except Exception as e:
                print("Sanddial Paint: brush_stroke_id set failed:", e)

        # 3. Fallback path: brush_active toggle (race-prone but functional).
        if not getattr(self, "_warned_no_stroke_id", False):
            print("Sanddial Paint WARNING: brush_stroke_id parm not found. "
                  "Rebuild the C++ HDA for reliable drag painting; falling "
                  "back to brush_active toggle (some strokes may be lost).")
            self._warned_no_stroke_id = True
        try:
            cur = int(node.evalParm("brush_active") or 0)
            node.parm("brush_active").set(0 if cur else 1)
        except Exception as e:
            print("Sanddial Paint: brush_active toggle failed:", e)


def createViewerStateTemplate():
    """Create and return the viewer state template."""
    template = hou.ViewerStateTemplate(
        STATE_NAME, "Erodibility Paint", hou.sopNodeTypeCategory()
    )
    template.bindFactory(State)
    template.bindIcon("SOP_paint")
    
    # Create the menu
    menu = hou.ViewerStateMenu("sanddial_paint_menu", "Particle Color Visualization")
    menu.addActionItem("visualize_nothing", "Nothing")
    menu.addSeparator()
    menu.addActionItem("visualize_erodibility", "Erodibility")
    menu.addActionItem("visualize_viability", "Viability")
    menu.addActionItem("visualize_stress", "Stress")
    menu.addActionItem("visualize_normals", "Normals")
    menu.addSeparator()
    menu.addActionItem("visualize_deflation", "Wind Deflation")
    menu.addActionItem("visualize_abrasion", "Wind Abrasion")
    # menu.addActionItem("visualize_water", "Water")
    menu.addActionItem("visualize_total", "Total Erosion")
    template.bindMenu(menu)
    
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
