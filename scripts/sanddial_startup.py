"""
sanddial_startup.py — Loaded automatically by Houdini at startup via
Documents/houdini21.0/python3.11libs/ (or equivalent path).

Responsibilities
----------------
1. Register the Environment Edit viewer state (the erodibility-paint state is
   already registered by the HDA's own ViewerStateInstall section).
2. Install a parameter-change callback on every Sanddial node that switches the
   active viewer state whenever viewport_mode changes.
3. Restore the callback when an existing Houdini session is opened.
"""

import hou
import math
import os
import sys
import types

# ── Constants ─────────────────────────────────────────────────────────────────
_SANDDIAL_TYPE_NAMES = ("V_sanddial", "V::sanddial::1.0", "sanddial")
_ENV_STATE           = "sop_sanddial_environment_edit"
try:
    _SCRIPTS_DIR = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "..", "cis6600", "sanddial", "scripts",
    )
except NameError:
    _SCRIPTS_DIR = r"C:\Users\V\_\penn\cis6600\sanddial\scripts"

# ── Viewer-state registration ─────────────────────────────────────────────────

def _load_env_state_from_hda():
    """
    Load and register the Environment Edit viewer state.
    Tries the HDA section first; falls back to the standalone .py file.
    """
    # 1. Try HDA section
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
            hou.ui.registerViewerState(tpl)
            print("Sanddial startup: registered", _ENV_STATE, "(from HDA section)")
            return True

    # 2. Fallback: adjacent scripts/ directory
    candidates = [
        os.path.normpath(os.path.join(os.path.dirname(__file__),
                                      "sanddial_env_state.py")),
        os.path.normpath(os.path.join(os.path.dirname(__file__),
                          "..", "_", "penn", "cis6600", "sanddial",
                          "scripts", "sanddial_env_state.py")),
        r"C:\Users\V\_\penn\cis6600\sanddial\scripts\sanddial_env_state.py",
    ]
    for path in candidates:
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as fh:
                src = fh.read()
            mod = types.ModuleType("sanddial_env_state_module")
            exec(compile(src, path, "exec"), mod.__dict__)
            sys.modules["sanddial_env_state_module"] = mod
            tpl = mod.createViewerStateTemplate()
            hou.ui.registerViewerState(tpl)
            print("Sanddial startup: registered", _ENV_STATE, f"(from {path})")
            return True

    print("Sanddial startup: WARNING — could not load Environment Edit viewer state.")
    return False


# ── Mode-switch callback ───────────────────────────────────────────────────────

def _viewport_mode_changed(node, parm_tuple=None, **kwargs):
    """
    Called whenever the viewport_mode parameter changes on a Sanddial node.
    Activates the appropriate viewer state (or returns to default select).
    """
    try:
        if node is None:
            return
            
        # If this was called from a node ParmTupleChanged event, check the name
        if parm_tuple and parm_tuple.name() != "viewport_mode":
            return
            
        parm = node.parm("viewport_mode")
        if not parm:
            return
            
        val = parm.eval()
        items = parm.menuItems()
        
        if val in items:
            mode = items.index(val)
        else:
            try:
                mode = int(val)
            except:
                mode = 0

        # Find the scene viewer forcefully using toolutils
        import toolutils
        viewer = toolutils.sceneViewer()
        if viewer is None:
            # Fallback
            desktop = hou.ui.curDesktop()
            viewer = desktop.paneTabOfType(hou.paneTabType.SceneViewer)
            
        if viewer is None:
            return

        # Crucial: ensure Scene Viewer is looking at the same network level as the node!
        try:
            viewer.setPwd(node.parent())
        except:
            pass

        # Force out of current view state by resetting to select first
        try:
            viewer.pane().setIsCurrentTab()
            viewer.setPwd(node.parent())
            viewer.setCurrentState('select')
        except:
            pass
            
        if mode == 1:   # Erodibility Paint
            node.setSelected(True, clear_all_selected=True)
            node.setCurrent(True, True)
            try:
                viewer.setPwd(node.parent())
                viewer.setCurrentState('sop_sanddial_erodibility_paint')
            except Exception as e:
                print("Sanddial: Failed entering paint state:", e)
        elif mode == 2: # Environment Edit
            node.setSelected(True, clear_all_selected=True)
            node.setCurrent(True, True)
            try:
                viewer.setCurrentState(_ENV_STATE)
            except hou.OperationFailed:
                print("Sanddial: env state failed.")
        else:           # View (default)
            pass # We already set select

    except Exception as exc:
        print("Sanddial _viewport_mode_changed error:", exc)


def _on_parm_tuple_changed(**kwargs):
    node = kwargs.get("node")
    parm_tuple = kwargs.get("parm_tuple")
    if node and parm_tuple and parm_tuple.name() == "viewport_mode":
        _viewport_mode_changed(**kwargs)


def _install_node_callback(node):
    """Attach the viewport_mode callback to a single Sanddial node."""
    # Remove stale callbacks first
    node.removeAllEventCallbacks()
    node.addEventCallback(
        (hou.nodeEventType.ParmTupleChanged,),
        _on_parm_tuple_changed
    )
    print(f"Sanddial: Installed reliable node event listener on {node.path()}")


def _install_callbacks_on_all_nodes():
    """Walk every node already in the scene and wire up Sanddial callbacks."""
    for node in hou.node("/").allSubChildren():
        try:
            # Find ANY node that has a viewport_mode parm
            if node.parm('viewport_mode') is not None:
                _install_node_callback(node)
        except Exception:
            pass


# ── Node-creation event ───────────────────────────────────────────────────────

def _on_node_created(event_type, **kwargs):
    node = kwargs.get("node")
    if node is None:
        return
    try:
        if node.type().name() in _SANDDIAL_TYPE_NAMES:
            _install_node_callback(node)
            print(f"Sanddial: installed callback on '{node.path()}'")
    except Exception as exc:
        print("Sanddial _on_node_created error:", exc)


# ── Entry point ───────────────────────────────────────────────────────────────

def _startup():
    # 1. Register the environment edit viewer state.
    _load_env_state_from_hda()

    # 2. Register the erodibility paint viewer state (standalone, not from HDA).
    try:
        paint_path = os.path.join(_SCRIPTS_DIR, "sanddial_paint_state.py")
        if os.path.exists(paint_path):
            with open(paint_path, "r", encoding="utf-8") as fh:
                paint_src = fh.read()
            import types as _t
            mod = _t.ModuleType('sanddial_paint_state')
            exec(compile(paint_src, paint_path, 'exec'), mod.__dict__)
            sys.modules['sanddial_paint_state'] = mod
            # register() is called automatically on import
            print("Sanddial startup: loaded paint state")
    except Exception as e:
        print("Sanddial startup: failed to load paint state:", e)

    # 3. Wire up the node-creation hook.
    hou.hipFile.addEventCallback(_on_scene_load)
    hou.node("/").addEventCallback(
        (hou.nodeEventType.ChildCreated,),
        _on_node_created,
    )

    # 4. Handle nodes already present in the scene (e.g. on session restore).
    _install_callbacks_on_all_nodes()

    print("Sanddial startup: complete")


def _on_scene_load(event_type, **kwargs):
    """Re-wire callbacks whenever a new .hip file is loaded."""
    if event_type in (hou.hipFileEventType.AfterLoad,
                      hou.hipFileEventType.AfterMerge):
        _install_callbacks_on_all_nodes()


# Run immediately when Houdini imports this module.
_startup()
