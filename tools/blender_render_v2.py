#!/usr/bin/env python3
"""Blender Render V2 (design scaffold)

This is a refactored starting point rather than a drop-in replacement.
Highlights:
- argparse CLI with subcommands
- template registry (diagnostic, portrait, turntable, ortho, clay)
- automatic output naming
- batch-ready architecture
- camera framing modes
- placeholder hooks for overlays, passes, contact sheets

Example:
  blender --background --python blender_render_v2_design.py -- render model.obj --template portrait
"""

from dataclasses import dataclass
from pathlib import Path
import argparse

@dataclass
class Template:
    name:str
    lens:float
    yaw:float
    pitch:float
    distance:float
    background:tuple

TEMPLATES={
    "diagnostic":Template("diagnostic",50,35,10,1.66,(0.18,0.18,0.20,1)),
    "portrait":Template("portrait",85,15,4,1.15,(0.12,0.12,0.12,1)),
    "ortho":Template("ortho",50,0,0,1.8,(1,1,1,1)),
    "clay":Template("clay",70,25,8,1.4,(0.16,0.16,0.16,1)),
    "turntable":Template("turntable",55,0,8,1.6,(0.18,0.18,0.20,1)),
}

def parser():
    p=argparse.ArgumentParser()
    sp=p.add_subparsers(dest="cmd",required=True)
    r=sp.add_parser("render")
    r.add_argument("obj")
    r.add_argument("-o","--output")
    r.add_argument("-t","--template",choices=TEMPLATES,default="diagnostic")
    r.add_argument("--frame",choices=["auto","full","torso","head"],default="auto")
    r.add_argument("--yaw",type=float)
    r.add_argument("--pitch",type=float)
    r.add_argument("--lens",type=float)
    r.add_argument("--overlay",action="store_true")
    r.add_argument("--passes",default="beauty")
    b=sp.add_parser("batch")
    b.add_argument("inputs",nargs="+")
    b.add_argument("-t","--template",default="turntable")
    b.add_argument("--angles",default="0,45,90,135,180,225,270,315")
    sp.add_parser("templates")
    return p

def main():
    args=parser().parse_args()
    if args.cmd=="templates":
        print("Templates:")
        [print(" -",k) for k in TEMPLATES]
        return
    if args.cmd=="render":
        tpl=TEMPLATES[args.template]
        out=args.output or str(Path(args.obj).with_suffix(".png"))
        print("Would render",args.obj)
        print("Output:",out)
        print("Template:",tpl)
        print("Frame:",args.frame)
        print("Passes:",args.passes)
        print("\nTODO: move existing Blender scene creation/import/render code into reusable functions.")
    elif args.cmd=="batch":
        print("Batch inputs:",args.inputs)
        print("Angles:",args.angles)
        print("TODO: iterate render() for every input/template/angle.")
if __name__=="__main__":
    main()
