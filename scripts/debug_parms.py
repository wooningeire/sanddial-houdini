import hou

for node in hou.node('/').allSubChildren():
    if 'sanddial' in node.type().name().lower():
        print("======== NODE:", node.path())
        ptg = node.parmTemplateGroup()
        for p in ptg.parmTemplates():
            try:
                lbl = p.label()
            except:
                lbl = "N/A"
            print(f"  Parm: name='{p.name()}', label='{lbl}', type={p.type()}")
