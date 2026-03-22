import hou
import toolutils

class SanddialState(object):
    """
    Python Viewer State for Sanddial.
    Displays a persistent prompt message to indicate the viewer state is active.
    """
    def __init__(self, state_name, scene_viewer):
        self.state_name = state_name
        self.scene_viewer = scene_viewer

    def onEnter(self, kwargs):
        self.scene_viewer.setPromptMessage("Sanddial Viewer State: Ready for Erodibility Paint / Environment Edit")

    def onInterrupt(self, kwargs):
        pass

    def onResume(self, kwargs):
        self.scene_viewer.setPromptMessage("Sanddial Viewer State: Ready for Erodibility Paint / Environment Edit")

    def onMouseEvent(self, kwargs):
        return False

def createViewerStateTemplate():
    """ Mandatory entry point to create and return the viewer state template to register. """
    state_typename = "sanddial_state"
    state_label = "Sanddial State"
    state_cat = hou.sopNodeTypeCategory()

    template = hou.ViewerStateTemplate(state_typename, state_label, state_cat)
    template.bindFactory(SanddialState)
    template.bindIcon("MISC_python")

    return template
