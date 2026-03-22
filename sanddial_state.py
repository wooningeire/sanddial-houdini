import hou
import viewerstate.utils as su

class State(object):
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)
    def onEnter(self, kwargs):
        self.scene_viewer.setPromptMessage("Sanddial Viewer State: Ready for Erodibility Paint / Environment Edit")

def createViewerStateTemplate():
    """ Mandatory entry point to create and return the viewer state 
        template to register. """

    state_typename = kwargs["type"].definition().sections()["DefaultState"].contents()
    state_label = "V::sanddial::1.0"
    state_cat = hou.sopNodeTypeCategory()

    template = hou.ViewerStateTemplate(state_typename, state_label, state_cat)
    template.bindFactory(State)
    template.bindIcon(kwargs["type"].icon())

    return template
