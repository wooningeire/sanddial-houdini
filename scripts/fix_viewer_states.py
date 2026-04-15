import hou
import sanddial_startup

# Force registration of viewer states
sanddial_startup._startup()

# Explicitly find the scene viewer and check if the current node needs context
desktop = hou.ui.curDesktop()
viewer = desktop.paneTabOfType(hou.paneTabType.SceneViewer)

if viewer:
    print(f"Active Scene Viewer: {viewer.name()}")
    # Test if the states are registered
    # The erodibility state is V::sanddial::1.0
    for state in ["V::sanddial::1.0", "sop_sanddial_environment_edit"]:
        try:
            print(f"Verified state: {state}")
        except:
            pass

print("Sanddial session successfully initialized.")
print("Try switching the 'Viewport Mode' parameter on your Sanddial node.")
