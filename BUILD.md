# ./tools/blender_render - V2
Upgrade the blender_render tool used to render objects as described in the ./tools/BLENDER_RENDER_V2.md build document. Build phases 1-4 to blender_render_v2.py.

Use the existing blender_render.py for reference and the following example typical use of it:
`objfile="$HOME/etc/git/guildlite-output/target_pn77_map148_20260708-032414"; blender --threads 12 --background --python tools/blender_render.py -- $objfile.obj //$objfile.png 60 --cycles-print-stats`

Use any of the object files in ../guildlite-output/*.obj for testing and diagnosis.
