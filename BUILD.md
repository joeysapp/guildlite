OBInvestigate the layer injected into the Guild Wars DX9 client and (if possible) build a Metal-compatible build into ./build.sh that creates a Mac GUI application to use the tool without a Guild Wars client. 

The intent is both developmental (build/test ImGui functionality) and functional. A primary use case: GUI control of injected Windows clients from macOS GUI. Stub injection and ideated 'dual-threaded' concepts could allow for reliable, live reloading of code and more.

Consider fully the project state and focus on lightweight development/testing. We currently have a one-liner to reboot a dll, but nothing more:
```
# Reboot
printf 'reboot\ncapture\n' > /tmp/gl-control; scp -q /tmp/gl-control guildlite-win:Documents/guildlite/control

# Turn off
printf 'off\ncapture\n' > /tmp/gl-control; scp -q /tmp/gl-control guildlite-win:Documents/guildlite/control

# Turn on - not sure if core is providing this
printf 'on\ncapture\n' > /tmp/gl-control; scp -q /tmp/gl-control guildlite-win:Documents/guildlite/control
```

# OPEN QUESTION
The Metal-GUI control from macOS was generally confirmed but an open idea/lower priority, but high interest and same ask: what about the exact same for iOS? In past experience this would expand the macOS build to an xcodeproj build no? An Apple Developer account to boot to iPhone?

Between a "no, not possible" and "yes, but harder" - am open to discussion. Complete macOS user and walled-garden user - the Linux space for Guild Wars is being opened but have seen no tooling or even mention of macOS. Admittedly this 'headless'/Guild Wars-less Guildlite surface will be more for development at first, it seems like a fun iOS experiment if it wouldn't be a headache/not worth the effort.

**Background: developing on the go, desktop/Windows computer is at home and did not set tunneling up - as a result, none of the gw-tool/Gw.exe tests and verifications will work and is OK.**


## NEXT BUILD
- Review ModelMod submodule for relevant information and potential breakthroughs for current model-export work (reliable, clean, target exports in a single snapshot button) [NOTE: Submodule checked out to main with DX11 code, may need to search GH release notes for last known DX9 build.]
 - Background: Last state of model exporting (first tooling provided by the Guildlite dll) was using A/B analysis and review of settings (./renders/MODEL-EXPORT-SETTINGS.md) There has been progress made and a classified and manually-confirmed models+renders+settings is in progress at ./RENDER-FEEDBACK (classifying output from `dev-tool` runs, an AI-driven visual inspection tool intended for general usage in Guildlite development.)
 - Before `dev-tool` work is continued it seems prudent to consider the ModelMod DX9 prior work for relevant help. It may be tertiary information for us - is there a chance we've gone about exporting wrong with vertex-shader approach instead of DAT exporing/otherwise? The model-exporter is certainly robust and promising (actual rendering) but if we can do exporting easier, consider that as a leapfrog. Consider that the "solo export" current goal could be done simply, then we pivot our renderer to be game-world/full renderer and export tool (all under the same hood.) Investigate.
