"""
sanddial_startup.py — Loaded automatically by Houdini at startup via
Documents/houdini21.0/python3.11libs/ (or equivalent path).

Registers the Erodibility Paint viewer state and exposes
_enter_state_from_button(), used when wiring PRM_CALLBACK buttons on the SOP.
"""

import hou
import os
import sys
import types

_PAINT_STATE = "sop_sanddial_erodibility_paint"

try:
    _SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
except NameError:
    _SCRIPTS_DIR = r"C:\Users\V\_\penn\cis6600\sanddial\scripts"


def _register_state_from_file(filename, module_name):
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
            return True
    return False


def _enter_state_from_button(node_path, mode):
    """
    Switch the Scene Viewer into the requested state.

    mode: 1 = Erodibility Paint, 0 = View (select)
    """
    try:
        node = hou.node(node_path)
        if node is None:
            return

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
            return

        try:
            viewer.pane().setIsCurrentTab()
        except Exception:
            pass
        try:
            viewer.setPwd(node.parent())
        except Exception:
            pass

        node.setSelected(True, clear_all_selected=True)
        node.setCurrent(True, True)

        if mode == 1:
            viewer.setCurrentState(_PAINT_STATE)
        else:
            viewer.setCurrentState("select")

    except Exception:
        pass


def _startup():
    _register_state_from_file("sanddial_paint_state.py", "sanddial_paint_state")


_startup()
