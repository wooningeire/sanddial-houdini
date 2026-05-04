"""
sanddial_startup.py — Loaded automatically by Houdini at startup via
Documents/houdini21.0/python3.11libs/ (or equivalent path).

Responsibilities
----------------
1. Register the Environment Edit viewer state.
2. Register the Erodibility Paint viewer state.
3. Expose _enter_state_from_button(), called directly by the C++ PRM_CALLBACK
   buttons on the SOP.  Because PRM_CALLBACK runs in the UI thread (not inside
   a cook), setCurrentState() works immediately — no deferred callbacks needed.
"""

import hou
import os
import sys
import types

# ── Constants ─────────────────────────────────────────────────────────────────
_SANDDIAL_TYPE_NAMES = ("V_sanddial", "V::sanddial::1.0", "sanddial")
_ENV_STATE           = "sop_sanddial_environment_edit"
_PAINT_STATE         = "sop_sanddial_erodibility_paint"

try:
    _SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
except NameError:
    _SCRIPTS_DIR = r"C:\Users\V\_\penn\cis6600\sanddial\scripts"

# ── Viewer-state registration ─────────────────────────────────────────────────

def _register_state_from_file(filename, module_name, description):
    """Load a viewer state from a .py file and register it. Returns True on success."""
    candidates = [
        os.path.join(_SCRIPTS_DIR, filename),
        os.path.join(_SCRIPTS_DIR, "..", "..", "cis6600", "sanddial", "scripts", filename),
        os.path.join(r"C:\Users\V\_\penn\cis6600\sanddial\scripts", filename),
    ]
    for path in candidates:
        path = os.path.normpath(path)
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as fh:
                src = fh.read()
            mod = types.ModuleType(module_name)
            exec(compile(src, path, "exec"), mod.__dict__)
            sys.modules[module_name] = mod
            tpl = mod.createViewerStateTemplate()
            try:
                hou.ui.unregisterViewerState(tpl.name())
            except Exception:
                pass
            hou.ui.registerViewerState(tpl)
            print(f"Sanddial startup: registered {description} (from {path})")
            return True
    print(f"Sanddial startup: WARNING — could not find {filename}")
    return False


def _load_env_state_from_hda():
    """Try HDA section first, fall back to standalone file."""
    for type_name in _SANDDIAL_TYPE_NAMES:
        node_type = hou.sopNodeTypeCategory().nodeTypes().get(type_name)
        if node_type is None:
            continue
        defn = node_type.definition()
        if defn is None:
            continue
        sections = defn.sections()
        if "ViewerStateModule_env" in sections:
            src = sections["ViewerStateModule_env"].contents()
            mod = types.ModuleType("sanddial_env_state_module")
            exec(compile(src, "ViewerStateModule_env", "exec"), mod.__dict__)
            sys.modules["sanddial_env_state_module"] = mod
            tpl = mod.createViewerStateTemplate()
            try:
                hou.ui.unregisterViewerState(tpl.name())
            except Exception:
                pass
            hou.ui.registerViewerState(tpl)
            print("Sanddial startup: registered", _ENV_STATE, "(from HDA section)")
            return True

    return _register_state_from_file(
        "sanddial_env_state.py", "sanddial_env_state_module", _ENV_STATE)


# ── Button entry point (called from C++ PRM_CALLBACK) ────────────────────────

def _enter_state_from_button(node_path, mode):
    """
    Switch the Scene Viewer into the requested state.

    Called directly by the C++ PRM_CALLBACK buttons (enter_paint_state /
    enter_env_state).  PRM_CALLBACK runs in the UI thread, outside any cook,
    so setCurrentState() is safe to call here without any deferral.

    mode: 1 = Erodibility Paint, 2 = Environment Edit, 0 = View (select)
    """
    try:
        node = hou.node(node_path)
        if node is None:
            print(f"Sanddial: node not found at '{node_path}'")
            return

        # Update the hidden viewport_mode indicator so the cook and viewer
        # states can read the current mode.
        try:
            node.parm("viewport_mode").set(mode)
        except Exception:
            pass

        import toolutils
        viewer = toolutils.sceneViewer()
        if viewer is None:
            desktop = hou.ui.curDesktop()
            viewer = desktop.paneTabOfType(hou.paneTabType.SceneViewer)
        if viewer is None:
            print("Sanddial: no Scene Viewer found")
            return

        # Bring the viewer pane to the front and set its network context.
        try:
            viewer.pane().setIsCurrentTab()
        except Exception:
            pass
        try:
            viewer.setPwd(node.parent())
        except Exception:
            pass

        # Make the Sanddial node current so the viewer state can find it.
        node.setSelected(True, clear_all_selected=True)
        node.setCurrent(True, True)

        if mode == 1:
            viewer.setCurrentState(_PAINT_STATE)
        elif mode == 2:
            viewer.setCurrentState(_ENV_STATE)
        else:
            viewer.setCurrentState("select")

    except Exception as exc:
        print("Sanddial _enter_state_from_button error:", exc)
        import traceback
        traceback.print_exc()


# ── Entry point ───────────────────────────────────────────────────────────────

def _startup():
    # Register viewer states.
    _load_env_state_from_hda()
    _register_state_from_file(
        "sanddial_paint_state.py", "sanddial_paint_state", _PAINT_STATE)

    print("Sanddial startup: complete")


_startup()
