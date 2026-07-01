Architect and build the remaining action items found in Sprint 3 of `guildlite` MVP ROADMAP document.

Use the following references and background in your planning and composition:
- `./etc/git/guildlite/DRAFT.md`: High-level proposal for Guild Wars 3D object tooling and creative tools.
- `./etc/git/guildlite/ROADMAP.md`: Actionable build items to get MVP in the air. Sprints 1 and 2 complete, Visual Studio builds failing looking to mass improvements and rethinking of ./build.sh. Look to **research item** in the POSIX sprint - building for iOS/iPad and Linux would be ideal in the future.
- `./etc/git/guildlite/SSH-AND-WINDOWS.md`: Pain points with lack of Windows interop with global runfiles and tools, referencing recent improvements to network keysharing and cross-platform builds.
- `./.dot/BUILD.md`: This document, placed in the filesystem git directory to provide access and knowledge of built fundamental tools missing from Windows (basic ssh setup and interop)
- `$ dot log -n15 | grep claude | wc -l`: Number of highly effective commits authored in  multi-tenant runfile repo (11/15)

# Background
The initial rough `guildlite` build script has been running with varying degrees of success - failures stemming from SCPing builds to a wrong directory, User and file permissions going afoul across network and likely very relevant in motivation - unfamiliarity/dislike of POSIX itself. Not from arrogance or lack of interest, but simply the friction for "fun programming" I found as a C# developer of 2 years (10 years ago.)

  The intent of `guildlite` is more creative than otherwise, but the idea of building a plugin for a game is a fun one I had not seriously considered since college; pulling out game objects to project and manipulate, learning multiple new skills (color/UV mapping of solids, .. effects/luminance and most importantly coding in POSIX) has me excited about both `guildlite` and possibly getting Windows classified as a fun platform in my mind.

  Described at this length to solidly quantify the team lacks the experience and time to work through Visual Studio and would far prefer to spend the effort successfully coupling a previously-thought incompatible platform into macOS/Linux and dip our toes into the high-compute realm (CUDA, AI) and world of 3D rendering and objects (Games, Simulations, etc.)

# Requests and Final Thoughts
- Verifying responses from $WINDOWS_HOST prior to acceptance (the Windows host's VS builds take quite a while and its ssh daemon dropped a good number of those waiting builds.)
- Suggestions on structuring repo/submodules and how to (possibly) submit a model-exporter-plugin to GWToolboxpp in the future after initial MVP built and working. Phases 1 and 2 of ROADMAP have been built in the submodule's local branch `guildlite` but considered forking for more freedom/less cruft as I build the tool for myself as the main user - the untracked changes are some fixes that I (think) I caught on their most recent pull - so **CAVEAT** you may run into some issues yourself; feel free to do initial builds, confirmations and tests on prior stable builds. I did have it built a number of times today, just not running my own plugin.
  - GitHub Stats: 8376 commits, 140 forks, 289 stars. Not against joining in the future or having my own fork I can toy with and send builds to my friends to use.
