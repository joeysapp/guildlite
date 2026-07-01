#!/usr/bin/env bash
# Deploy and build GWToolboxpp on the Windows remote, then sync intellisense back

set -euo pipefail

REMOTE="bob@bobmobile.local"
# Using a path relative to the user's home directory on Windows
REMOTE_DIR="guildlite"

echo "========================================"
echo "1. Archiving source code"
echo "========================================"
# Uses git archive on the Mac to guarantee we *only* package pristine source code tracked by git.
git archive --format=tar -o guildlite_src.tar HEAD
cd GWToolboxpp && git archive --format=tar --prefix=GWToolboxpp/ HEAD -o ../sub.tar && cd ..
tar -r -f guildlite_src.tar @sub.tar
rm sub.tar
gzip -f guildlite_src.tar

echo "========================================"
echo "2. Transferring archive via SCP"
echo "========================================"
# Use -4 to match the SSH/network tooling preferences in the dotfiles
scp -4 guildlite_src.tar.gz "$REMOTE:~/"

echo "========================================"
echo "3. Extracting & Triggering Build on Windows"
echo "========================================"
echo "Starting Remote CMake Build... (Streaming output back for audit)"
# We use cmd.exe syntax (&) since the Windows OpenSSH server defaults to cmd.
# The output is piped back over SSH and saved to build_audit.log on macOS
# Capturing stderr (2>&1) and using pipefail ensures we can reliably audit errors.
ssh -4 "$REMOTE" "mkdir $REMOTE_DIR 2>NUL & cd $REMOTE_DIR & tar -xzf ../guildlite_src.tar.gz & cd GWToolboxpp & set VCPKG_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg & cmake --preset vcpkg -DCMAKE_EXPORT_COMPILE_COMMANDS=ON & cmake --build build --config Debug" 2>&1 | tee build_audit.log
echo "Build completed! Full log saved to build_audit.log"

echo "========================================"
echo "4. Syncing compile_commands.json for macOS IDE support"
echo "========================================"
mkdir -p GWToolboxpp/build
scp -4 "$REMOTE:$REMOTE_DIR/GWToolboxpp/build/compile_commands.json" GWToolboxpp/build/compile_commands.json 2>/dev/null || true

if [ -f "GWToolboxpp/build/compile_commands.json" ]; then
    echo "Adjusting Windows paths in compile_commands.json to macOS local paths..."
    # A simple Python script to rewrite the paths in the compile_commands.json
    python3 -c "
import json
import os

local_root = os.path.abspath(os.path.join(os.getcwd(), 'GWToolboxpp')).replace('\\\\', '/')
try:
    with open('GWToolboxpp/build/compile_commands.json', 'r') as f:
        cmds = json.load(f)
    
    for cmd in cmds:
        # Replace the remote Windows base path with our local macOS base path
        # We find the index of '/GWToolboxpp' to figure out the Windows root
        win_path = cmd['directory'].replace('\\\\', '/')
        idx = win_path.find('/GWToolboxpp')
        if idx != -1:
            win_root = win_path[:idx]
            
            # Update directory, file, and command strings
            cmd['directory'] = cmd['directory'].replace('\\\\', '/').replace(win_root, local_root)
            cmd['file'] = cmd['file'].replace('\\\\', '/').replace(win_root, local_root)
            cmd['command'] = cmd['command'].replace('\\\\', '/').replace(win_root, local_root)
            
    with open('GWToolboxpp/build/compile_commands.json', 'w') as f:
        json.dump(cmds, f, indent=2)
    print('Successfully generated local macOS compile_commands.json!')
except Exception as e:
    print('Failed to parse or update compile_commands.json:', e)
"
    
    # Symlink it to the root of GWToolboxpp so clangd picks it up automatically
    ln -sf build/compile_commands.json GWToolboxpp/compile_commands.json
fi

echo "========================================"
echo "Deployment and build pipeline complete!"
echo "========================================"
