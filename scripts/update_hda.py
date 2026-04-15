"""
update_hda.py — Run this script using hython to embed the Environment Edit 
viewer state and install parameter callbacks into the Sanddial HDA.

Usage (from command line):
    hython update_hda.py
"""

import hou
import os

# ── Locate source files ──────────────────────────────────────────────────────
try:
    _SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
except NameError:
    _SCRIPTS_DIR = os.path.dirname(os.path.abspath(r"C:\Users\V\_\penn\cis6600\sanddial\scripts\update_hda.py"))
_ENV_STATE_PATH = os.path.join(_SCRIPTS_DIR, "sanddial_env_state.py")

with open(_ENV_STATE_PATH, "r", encoding="utf-8") as fh:
    _ENV_STATE_SRC = fh.read()

# ── Callback Scripts ─────────────────────────────────────────────────────────

VIEWPORT_MODE_CALLBACK = r"""
import hou

try:
    node = kwargs['node']
    parm = node.parm('viewport_mode')
    val = parm.eval() if parm else node.evalParm('viewport_mode')
    items = parm.menuItems() if parm else []
    
    if val in items:
        mode = items.index(val)
    else:
        try:
            mode = int(val)
        except:
            mode = 0

    import toolutils
    viewer = toolutils.sceneViewer()
    if viewer is None:
        desktop = hou.ui.curDesktop()
        viewer = desktop.paneTabOfType(hou.paneTabType.SceneViewer)

    if viewer:
        try:
            viewer.pane().setIsCurrentTab()
            viewer.setCurrentState('select')
        except:
            pass

        if mode == 1:
            node.setSelected(True, clear_all_selected=True)
            node.setCurrent(True, True)
            try:
                viewer.enterCurrentNodeState()
            except:
                viewer.setCurrentState('V::sanddial::1.0', node=node)
        elif mode == 2:
            node.setSelected(True, clear_all_selected=True)
            node.setCurrent(True, True)
            try:
                viewer.setCurrentState('sop_sanddial_environment_edit', node=node)
            except:
                pass
                
        hou.ui.statusMessage(f"Sanddial: Entered mode {mode}")
except Exception as e:
    print("Sanddial callback error:", e)
"""

ON_CREATED_SCRIPT = r"""
try:
    import sanddial_startup
except:
    pass
"""

def _find_sanddial_def():
    """Return the HDADefinition for the V::sanddial::1.0 SOP."""
    # First, try to find an active Sanddial node in the scene to get the exact definition!
    try:
        for node in hou.node('/').allSubChildren():
            if 'sanddial' in node.type().name().lower() and node.parm('viewport_mode'):
                defn = node.type().definition()
                if defn:
                    return defn
    except:
        pass
        
    cat = hou.sopNodeTypeCategory()
    for name in ("V::sanddial::1.0", "V_sanddial", "sanddial"):
        node_type = cat.nodeTypes().get(name)
        if node_type is not None:
            defn = node_type.definition()
            if defn is not None:
                return defn
    return None

def main():
    print("Locating active Sanddial node in scene...")
    target_node = None
    for node in hou.node('/').allSubChildren():
        if 'sanddial' in node.type().name().lower() and node.parm('viewport_mode'):
            target_node = node
            break

    if not target_node:
        print("Error: Could not find a Sanddial node with a 'viewport_mode' parameter in the scene.")
        return

    print(f"Found SOP node: {target_node.path()}")
    defn = target_node.type().definition()

    # ── 1. Embed Environment Edit state source ───────────────────────────────
    if defn:
        env_section_name = "ViewerStateModule_env"
        defn.addSection(env_section_name, _ENV_STATE_SRC)
        print(f"  Embedded section '{env_section_name}' to HDA")

        # ── 2. Add ViewerStateInstall logic ──────────────────────────────────────
        INSTALL_KEY = "ViewerStateInstall"
        sections = defn.sections()
        existing_install = sections[INSTALL_KEY].contents() if INSTALL_KEY in sections else ""
        
        MARKER = "# [sanddial_env_state registered]"
        if MARKER not in existing_install:
            addition = f"""
{MARKER}
import importlib, types, hou
def _reg_env():
    try:
        node_type = hou.nodeType(hou.sopNodeTypeCategory(), '{defn.nodeTypeName()}')
        src = node_type.definition().sections()['{env_section_name}'].contents()
        mod = types.ModuleType('sanddial_env_state_module')
        exec(compile(src, '{env_section_name}', 'exec'), mod.__dict__)
        import sys; sys.modules['sanddial_env_state_module'] = mod
        tpl = mod.createViewerStateTemplate()
        hou.ui.registerViewerState(tpl)
    except Exception as e:
        print("Sanddial: Registration error:", e)
_reg_env()
"""
            new_install = existing_install.rstrip() + "\n" + addition
            defn.addSection(INSTALL_KEY, new_install)
            print(f"  Updated section '{INSTALL_KEY}'")

        defn.addSection("OnCreated", ON_CREATED_SCRIPT)

    # ── 3. Set Parameter Callback on Viewport Mode on the SOP instance ──────────
    ptg = target_node.parmTemplateGroup()
    pt = ptg.find('viewport_mode')
    if pt:
        # Update the callback script string to use the actual parameter name!
        patched_callback = VIEWPORT_MODE_CALLBACK.replace("evalParm('viewport_mode')", "evalParm('viewport_mode')")
        
        # Use tags to set the callback script, avoiding missing methods on some ParmTemplate subclasses
        tags = pt.tags()
        tags["script_callback"] = patched_callback
        tags["script_callback_language"] = "python"
        pt.setTags(tags)
        
        ptg.replace('viewport_mode', pt)
        target_node.setParmTemplateGroup(ptg)
        print(f"  Installed callback via tags directly on the SOP's 'viewport_mode' parameter")
        
        # Save changes back to HDA if possible
        if defn:
            defn.updateFromNode(target_node)
            defn.save(defn.libraryFilePath())
            print(f"Successfully updated HDA definition: {defn.libraryFilePath()}")
    else:
        print("  Warning: 'viewport_mode' not found in the SOP's parm interface.")

if __name__ == "__main__":
    main()
