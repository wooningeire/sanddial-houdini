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
_VIEW_STATE_PATH = os.path.join(_SCRIPTS_DIR, "sanddial_view_state.py")

with open(_ENV_STATE_PATH, "r", encoding="utf-8") as fh:
    _ENV_STATE_SRC = fh.read()
with open(_VIEW_STATE_PATH, "r", encoding="utf-8") as fh:
    _VIEW_STATE_SRC = fh.read()

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
            viewer.setPwd(node.parent())
        except:
            pass

        if mode == 0:
            node.setSelected(True, clear_all_selected=True)
            node.setCurrent(True, True)
            try:
                viewer.setCurrentState('sop_sanddial_view', node=node)
            except:
                pass
        elif mode == 1:
            node.setSelected(True, clear_all_selected=True)
            node.setCurrent(True, True)
            try:
                viewer.setCurrentState('sop_sanddial_erodibility_paint')
            except:
                pass
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
    print("Locating active Sanddial nodes in scene...")
    target_nodes = []
    seen_paths = set()
    
    # Pass 1: Find ANY node that has a 'viewport_mode' parameter
    for node in hou.node('/').allSubChildren():
        if node.parm('viewport_mode') is not None and node.path() not in seen_paths:
            target_nodes.append(node)
            seen_paths.add(node.path())
            print(f"  Found node (by parm): {node.path()}  type={node.type().name()}")
    
    # Pass 2: Also find nodes by type name containing 'sanddial'
    for node in hou.node('/').allSubChildren():
        if 'sanddial' in node.type().name().lower() and node.path() not in seen_paths:
            target_nodes.append(node)
            seen_paths.add(node.path())
            print(f"  Found node (by type): {node.path()}  type={node.type().name()}")

    if not target_nodes:
        print("Error: Could not find any Sanddial nodes in the scene.")
        return

    # For the definition (source embedding), pick a node that has an actual HDA definition
    primary_node = None
    for n in target_nodes:
        if n.type().definition() is not None:
            primary_node = n
            break
    if primary_node is None:
        primary_node = target_nodes[0]
    
    print(f"Primary node for HDA definition: {primary_node.path()} (type={primary_node.type().name()})")
    defn = primary_node.type().definition()
    
    if defn is None:
        for name in ("V::sanddial::1.0", "V_sanddial"):
            try:
                defn = hou.nodeType(hou.sopNodeTypeCategory(), name).definition()
                if defn:
                    break
            except:
                pass

    # ── 1. Embed Environment Edit & Paint state sources ───────────────────────────────
    if defn:
        env_section_name = "ViewerStateModule_env"
        defn.addSection(env_section_name, _ENV_STATE_SRC)
        view_section_name = "ViewerStateModule_view"
        defn.addSection(view_section_name, _VIEW_STATE_SRC)
        print(f"  Embedded section '{env_section_name}' and '{view_section_name}' to HDA")

        # Also embed the paint state back into the main ViewerStateModule!
        try:
            paint_path = os.path.join(_SCRIPTS_DIR, "sanddial_paint_state.py")
            if os.path.exists(paint_path):
                with open(paint_path, "r", encoding="utf-8") as fh:
                    paint_src = fh.read()
                defn.addSection("ViewerStateModule", paint_src)
                print(f"  Embedded section 'ViewerStateModule' back into HDA ({defn.nodeTypeName()})")
                
                # CRITICAL: Also embed it into the inner C++ SOP HDA just in case Houdini prefers it!
                inner_defn = hou.nodeType(hou.sopNodeTypeCategory(), "V::sanddial::1.0").definition()
                if inner_defn and inner_defn != defn:
                    inner_defn.addSection("ViewerStateModule", paint_src)
                    print(f"  Embedded section 'ViewerStateModule' back into inner HDA (V::sanddial::1.0)")
        except Exception as e:
            print("  Failed to embed ViewerStateModule:", e)

        # ── 2. Add ViewerStateInstall logic ──────────────────────────────────────
        INSTALL_KEY = "ViewerStateInstall"
        sections = defn.sections()
        existing_install = sections[INSTALL_KEY].contents() if INSTALL_KEY in sections else ""
        
        MARKER = "# [sanddial_view_state registered]"
        if MARKER not in existing_install:
            addition = f"""
{MARKER}
import importlib, types, hou
def _reg_states():
    try:
        node_type = hou.nodeType(hou.sopNodeTypeCategory(), '{defn.nodeTypeName()}')
        
        # Register ENV state
        src_env = node_type.definition().sections()['{env_section_name}'].contents()
        mod_env = types.ModuleType('sanddial_env_state_module')
        exec(compile(src_env, '{env_section_name}', 'exec'), mod_env.__dict__)
        sys.modules['sanddial_env_state_module'] = mod_env
        hou.ui.registerViewerState(mod_env.createViewerStateTemplate())
        
        # Register VIEW state
        src_view = node_type.definition().sections()['ViewerStateModule_view'].contents()
        mod_view = types.ModuleType('sanddial_view_state_module')
        exec(compile(src_view, 'ViewerStateModule_view', 'exec'), mod_view.__dict__)
        sys.modules['sanddial_view_state_module'] = mod_view
        hou.ui.registerViewerState(mod_view.createViewerStateTemplate())
        
        # Register Paint state natively if it exists
        if 'ViewerStateModule' in node_type.definition().sections():
            src_paint = node_type.definition().sections()['ViewerStateModule'].contents()
            mod_paint = types.ModuleType('sanddial_paint_state_module')
            exec(compile(src_paint, 'ViewerStateModule', 'exec'), mod_paint.__dict__)
            sys.modules['sanddial_paint_state_module'] = mod_paint
            hou.ui.registerViewerState(mod_paint.createViewerStateTemplate())
    except Exception as e:
        print("Sanddial: Registration error:", e)
_reg_states()
"""
            new_install = existing_install.rstrip() + "\n" + addition
            defn.addSection(INSTALL_KEY, new_install)
            print(f"  Updated section '{INSTALL_KEY}'")

        defn.addSection("OnCreated", ON_CREATED_SCRIPT)

    # ── 3. Set Parameter Callback on Viewport Mode on the SOP instances ──────────
    # We must patch BOTH the outer wrapper and the inner SOP, so wherever the user 
    # clicks, the SceneViewer forcibly switches.
    patched_callback = VIEWPORT_MODE_CALLBACK.replace("evalParm('viewport_mode')", "evalParm('viewport_mode')")
    
    for t_node in target_nodes:
        ptg = t_node.parmTemplateGroup()
        pt = ptg.find('viewport_mode')
        
        # Fallback: Search by label if the internal name differs
        if not pt:
            for p in ptg.entries():
                if hasattr(p, 'label') and 'Viewport Mode' in p.label():
                    pt = p
                    break
            # Still not found? Look deeply
            if not pt:
                for p in ptg.parmTemplates():
                    if 'Viewport Mode' in p.label():
                        pt = p
                        break

        if pt:
            parm_name = pt.name()
            patched_callback = VIEWPORT_MODE_CALLBACK.replace("evalParm('viewport_mode')", f"evalParm('{parm_name}')")
            patched_callback = patched_callback.replace("parm('viewport_mode')", f"parm('{parm_name}')")
            
            tags = pt.tags()
            tags["script_callback"] = patched_callback
            tags["script_callback_language"] = "python"
            pt.setTags(tags)
            
            try:
                ptg.replace(parm_name, pt)
                t_node.setParmTemplateGroup(ptg)
                print(f"  Installed tags on {t_node.path()} (Parm label: {pt.label()}, name: {parm_name})")
            except Exception as e:
                print(f"  Failed to save tags on {t_node.path()}: {e}")
            
            # Save outer HDA
            if t_node.type().definition():
                try:
                    hda_defn = t_node.type().definition()
                    hda_defn.updateFromNode(t_node)
                    hda_defn.save(hda_defn.libraryFilePath())
                    print(f"  Updated HDA definition for {t_node.path()}")
                except:
                    pass

    # ── 4. Live Hot-Reload Viewer States ─────────────────────────────────────────
    print("Performing live hot-reload of Viewer States...")
    try:
        import sys, types as _types
        paint_path = os.path.join(_SCRIPTS_DIR, "sanddial_paint_state.py")
        if os.path.exists(paint_path):
            with open(paint_path, "r", encoding="utf-8") as fh:
                paint_src = fh.read()
            mod_paint = _types.ModuleType('sanddial_paint_state_module')
            exec(compile(paint_src, 'ViewerStateModule', 'exec'), mod_paint.__dict__)
            sys.modules['sanddial_paint_state_module'] = mod_paint
            
            tpl = mod_paint.createViewerStateTemplate()
            state_name = tpl.typeName()
            
            # Unregister old state first (ignore error if not registered)
            try:
                hou.ui.unregisterViewerState(state_name)
                print(f"  Unregistered old state '{state_name}'")
            except:
                pass
            
            # Register new state
            hou.ui.registerViewerState(tpl)
            print(f"  Registered new state '{state_name}'")
            
            # Force the viewer to exit and re-enter the state so the new
            # State class instance is created with the updated code
            try:
                import toolutils
                viewer = toolutils.sceneViewer()
                if viewer is None:
                    viewer = hou.ui.paneTabOfType(hou.paneTabType.SceneViewer)
                if viewer:
                    # Exit current state
                    viewer.setCurrentState('select')
                    # Find the sanddial node and set viewer context
                    for node in hou.node('/').allSubChildren():
                        if node.parm('viewport_mode') is not None:
                            node.setSelected(True, clear_all_selected=True)
                            node.setCurrent(True, True)
                            # Ensure viewer is at the SOP level
                            viewer.setPwd(node.parent())
                            break
                    # Enter our standalone paint state or view state
                    if node.evalParm('viewport_mode') == 1:
                        viewer.setCurrentState('sop_sanddial_erodibility_paint')
                    else:
                        viewer.setCurrentState('sop_sanddial_view')
                    print("  Re-entered appropriate state")
            except Exception as e2:
                print("  Could not auto-re-enter state:", e2)
                print("  Please switch viewport mode to View and back to Erodibility Paint")
            
            print("  Hot-reload complete!")
    except Exception as e:
        print("  Failed to hot-reload paint state:", e)
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
