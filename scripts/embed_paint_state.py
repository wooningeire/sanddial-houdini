# embed_paint_state.py
# Hot-reloads the Sanddial erodibility paint viewer state from disk.
# Paste the exec(open(...).read()) line from setup.md into the Houdini Python Shell.

import hou
import os
import sys
import types

_SCRIPTS_DIR = r"C:\Users\V\_\penn\cis6600\sanddial\scripts"
_PAINT_PATH  = os.path.join(_SCRIPTS_DIR, "sanddial_paint_state.py")
_STATE_NAME  = "sop_sanddial_erodibility_paint"

# ── Load and register ─────────────────────────────────────────────────────────
with open(_PAINT_PATH, "r", encoding="utf-8") as fh:
    paint_src = fh.read()

mod = types.ModuleType("sanddial_paint_state")
exec(compile(paint_src, _PAINT_PATH, "exec"), mod.__dict__)
sys.modules["sanddial_paint_state"] = mod

try:
    hou.ui.unregisterViewerState(_STATE_NAME)
    print(f"Unregistered old state '{_STATE_NAME}'")
except Exception:
    pass

hou.ui.registerViewerState(mod.createViewerStateTemplate())
print(f"Registered updated state '{_STATE_NAME}'")

# ── Re-enter if viewer is currently in the paint state ───────────────────────
try:
    import toolutils
    viewer = toolutils.sceneViewer()
    if viewer and viewer.currentState() == _STATE_NAME:
        viewer.setCurrentState("select")
        for node in hou.node("/").allSubChildren():
            if node.parm("viewport_mode") is not None:
                node.setSelected(True, clear_all_selected=True)
                node.setCurrent(True, True)
                viewer.setPwd(node.parent())
                break
        viewer.setCurrentState(_STATE_NAME)
        print("Re-entered paint state.")
except Exception as e:
    print(f"Note: could not auto-re-enter state ({e})")

print("Done.")
