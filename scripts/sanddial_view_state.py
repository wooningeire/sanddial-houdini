"""
Sanddial View Viewer State
--------------------------
Provides a visualization dropdown in the viewport.
"""

import hou

STATE_NAME = "sop_sanddial_view"

class State(object):
    def __init__(self, state_name, scene_viewer):
        self.state_name = state_name
        self.scene_viewer = scene_viewer
        self._node = None
        
        # Find the sanddial node
        for n in hou.node('/').allSubChildren():
            if n.parm('viewport_mode') is not None:
                self._node = n
                break

    def onEnter(self, kwargs):
        self._node = kwargs.get("node", None) or self._node
        print("Sanddial View: onEnter")

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
                print("Sanddial View Menu error:", e)

def createViewerStateTemplate():
    template = hou.ViewerStateTemplate(
        STATE_NAME, "Sanddial View", hou.sopNodeTypeCategory()
    )
    template.bindFactory(State)
    
    # Create the menu
    menu = hou.ViewerStateMenu("sanddial_view_menu", "Visualization")
    
    # Add options
    menu.addActionItem("visualize_nothing", "Nothing")
    menu.addSeparator()
    menu.addActionItem("visualize_erodibility", "Erodibility")
    menu.addActionItem("visualize_viability", "Viability")
    menu.addActionItem("visualize_stress", "Stress")
    menu.addActionItem("visualize_normals", "Normals")
    menu.addSeparator()
    menu.addActionItem("visualize_deflation", "Wind Deflation")
    menu.addActionItem("visualize_abrasion", "Wind Abrasion")
    menu.addActionItem("visualize_water", "Water")
    menu.addActionItem("visualize_total", "Total Erosion")
    
    template.bindMenu(menu)
    return template

def register():
    try:
        hou.ui.unregisterViewerState(STATE_NAME)
    except:
        pass
    tpl = createViewerStateTemplate()
    hou.ui.registerViewerState(tpl)
    print(f"Sanddial: Registered viewer state '{STATE_NAME}'")

if __name__ == "__main__":
    register()
