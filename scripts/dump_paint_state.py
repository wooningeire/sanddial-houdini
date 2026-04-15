import hou
import os

try:
    defn = hou.nodeType(hou.sopNodeTypeCategory(), "V::sanddial::1.0").definition()
    if defn:
        sections = defn.sections()
        if "ViewerStateModule" in sections:
            src = sections["ViewerStateModule"].contents()
            out_path = r"C:\Users\V\_\penn\cis6600\sanddial\scripts\sanddial_paint_state.py"
            with open(out_path, "w", encoding="utf-8") as f:
                f.write(src)
            print(f"Dumped ViewerStateModule to {out_path}")
        else:
            print("No ViewerStateModule found in HDA.")
    else:
        print("HDA definition not found.")
except Exception as e:
    print("Error:", e)
