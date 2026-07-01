# SSH, WINDOWS

# Below are several helpful commands built recently for linux/macOS SSH, git and event builds (emacs built and running from source cross platform):

# ~/etc/ssh/ssh-commands.sh - ssh-install-local-identity to setup new remotes (no POSIX/Windows. Admittedly a bit more difficult without OpenSSH installed and hooking it into Windows - this has been done on currenet windows remote but is missing much of necessary config)

# ~/etc/term/{dot,bash,windows-utils.sh} - The difficulty of getting the macOS/linux repo onto Windows is readily apparent with its missing of common functions and seemingly tempermental responses, but getting an MVP for this would be a great entry into hooking Windows into the dot/code pipelines.
# The ~/.dot/README.md only provides a rough outline of how to set a new machine up, much less a new Windows machine. Several months have been spent tinkering with WSL, not with much success - Window's strengths do not lie in emulating Linux. A windows-utils or even PowerShell addition into term-utils for alias/helpers and a general guide to get POSIX up in .dot are the #2 missing item for dot - some of it may encroach into guildlite's Sprint 3, unclear.

# dot show 498a2fde8228d237195280d0c0a89ad5f68d7d7c: ssh: Include-based config, kill the manual per-host key copy, harden pubkey-share
# Recent addition for quick and simple ssh setup. #1 missing item for Windows - to the point of generating a keypair and physically loading it on that way would suffice to get the client/host handshakes and configs reliably stable.

# dot show 783f88ef4b02eeb5b8d2bfce8f38befc88944450: shell: harden zsh/bash startup; emacs-in-bash; lazy LLM env
# Most recent Zshell improvements for network-wide syncing and runscripts. Potential reference to how a Bash or PowerShell equivalent could be structured / shared.
