#!/usr/bin/env bash
# Intent:
# - Write code on macOS, verifying here outside of PowerShell/CapsLock land
# - Send over to Windows 10 machine that has Visual Studio (WIP) + Guild Wars (Done), then:
#  - Does the code compile?
#  - Does the code run?
#  - Does the code do what it's supposed to?

# [TODO] I almost prefer improving the script's logic instead of this, but that's just me, especially after Visual Studio failing for the Nth time with no auditable log or way to diagnose the actual failure.
# set -euo pipefail

# Is it worth checking to see if our local DNS-unix-Windows triangle is fragile enough to be causing SSH drops from the custom names? They work until we're hammering tests
# REMOTE="bob@bobmobile.local"
REMOTE="$GUILDLITE_REMOTE"

# [TODO] Using a path relative to the user's home directory on Windows - do we need to escape / to \\? Should we just write to tmp or something? I'm unfamiliar with Windows development much less between macOS and Windows.
REMOTE_DIR="etc/git/guildlite"
# REMOTE_DIR="etc/git/guildlite-debug"

VERBOSE=""
while [[ $# -gt 0 ]]; do
				case $1 in
								-v|--verbose|-d|--debug)
												VERBOSE="-v"
												shift
												;;
								-h|--host|-r|--remote)
												REMOTE="$1"
												shift
												;;
								-d|--dir|-p|--path|)
								REMOTE_DIR="$1"
								shift
								;;
								*)
												echo "Unknown option: $1"
												exit 1
												;;
				esac
done

# Combine our own 'temp' working git repo with the GWToolboxpp repo for compiling. This might be wrong in the build chain, time will tell. Seems to work OK.
echo "1. Archiving git to tar for network transfer to:\n  $REMOTE\n  $REMOTE_DIR"
git archive --format=tar -o tmp-guildlite.tar HEAD

cd GWToolboxpp
git archive --format=tar --prefix=GWToolboxpp/ HEAD -o ../tmp-gwtb.tar
cd ..

# Combine the two
tar -r -f tmp-guildlite.tar @tmp-gwtb.tar
rm tmp-gwtb.tar
gzip -f tmp-guildlite.tar

echo "2. Transferring archive via SCP"
scp tmp-guildlite.tar.gz "$REMOTE:~/"

echo "3. Extracting & Triggering Build on Windows"
echo "Starting Remote CMake Build... (Streaming output back for audit)"

# [TODO] Below is from StackOverflow. Completely unclear if any of this is right

# We use cmd.exe syntax (&) since the Windows OpenSSH server defaults to cmd.
# The output is piped back over SSH and saved to build_audit.log on macOS
# Capturing stderr (2>&1) and using pipefail ensures we can reliably audit errors.

# [REVIEW] Yeah we need to rethink this. I think this is the biggest failure point currently.
# In my mind `deploy_to_windows.sh` should be a `build.sh` that can deploy to a remote and tell it to build as well as build from a target locally. I imagine these two really valuable cases:
# 0. Sending our plugin to be built inside a working GWToolboxpp project, started up and verified via logfiles over network
# 1. Sending GWToolboxpp + our plugin to verify new builds or changes work
# 2. Running `bash ./build.sh` on the Windows 10 machine, having the .dll dropped into the *correct*/flagged $GW_TOOLBOX_PLUGIN_DIR. (NOTE: I have no clue where this is, I think default is C:/Users/<username>/Documents/GWToolboxpp/plugins, but I cannot verify at this time)

# Below is .. not working. Old vars. The vcpkg and general cmake I _think_ worked, but the issue is - can we verify we can do headless Visual Studio builds yet? Stuck here.

# ssh $SSH_OPTS "$REMOTE" "mkdir $REMOTE_DIR 2>NUL & cd $REMOTE_DIR & tar -xzf ../guildlite_src.tar.gz & cd GWToolboxpp & set VCPKG_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg & cmake --preset vcpkg -DCMAKE_EXPORT_COMPILE_COMMANDS=ON & cmake --build build --config Debug" 2>&1 | tee build_audit.log <-- doesn't need a log, it just needs to actually output what it got unless idk, --silent.
