<#
  build_remote.ps1 -- Windows-side worker for the Guildlite build pipeline.

  Driven by ~/etc/git/guildlite/build.sh over ssh (PowerShell is the chosen remote
  shell). Fully headless: NO Visual Studio GUI -- just the CMake "vcpkg" preset +
  MSBuild via the VS generator, exactly like GWToolbox CI (.github/workflows/cmake.yml).

  Modes:
    -Mode Doctor   audit toolchain/paths, print a checklist + DOCTOR_RESULT=PASS|FAIL
    -Mode Launch   register + start a detached Scheduled Task that runs -Mode Worker,
                   then return immediately (survives the ssh session dropping)
    -Mode Worker   extract -> configure -> build -> verify -> deploy -> write markers
    -Mode Status   print the run's markers as KEY=VALUE (poll target for build.sh)

  All relative paths resolve against %USERPROFILE%. Markers live in
  %USERPROFILE%\.guildlite\<RunId>\ ; build.sh fetches from .guildlite/<RunId>/.
#>
param(
    [ValidateSet('Doctor','Launch','Worker','Status')] [string]$Mode = 'Worker',
    [string]$RunId    = '',
    [string]$Config   = 'RelWithDebInfo',
    [string]$SrcDir   = 'src/guildlite',
    [string]$PluginDir= 'Documents/GWToolboxpp/plugins',
    [ValidateSet('plugin','launcher','guildlite','datcore')] [string]$Kind = 'plugin',
    [string]$InstallDir= '',
    [string]$Tarball  = '',
    [string]$VcpkgRoot= '',
    [switch]$Clean,
    [switch]$NoDeploy
)

# NOT 'Stop': native build tools (cmake/vcpkg/tar) write progress to stderr, and under
# Stop a `& tool 2>&1` raises a spurious NativeCommandError that aborts the build right
# after configure. We gate on explicit $LASTEXITCODE checks + `throw` for real failures.
$ErrorActionPreference = 'Continue'
$Profile2 = $env:USERPROFILE

function Now-Epoch { [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() }
function Resolve-HomePath([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return $Profile2 }
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    return (Join-Path $Profile2 $p)
}
# GWToolbox loads plugins from Documents\GWToolboxpp\<COMPUTERNAME>\plugins. Use the hint
# if it exists, else derive from COMPUTERNAME, else fall back to the hint (to be created).
function Resolve-PluginDir([string]$hint) {
    $p = Resolve-HomePath $hint
    if (Test-Path $p) { return $p }
    $byName = Join-Path $Profile2 "Documents\GWToolboxpp\$env:COMPUTERNAME\plugins"
    if (Test-Path $byName) { return $byName }
    return $p
}
# Launcher + main DLL live one level ABOVE the per-machine <COMPUTERNAME> dir:
# %USERPROFILE%\Documents\GWToolboxpp\{GWToolbox.exe,GWToolboxdll.dll}. Derive that
# root from the resolved plugin dir (…\GWToolboxpp\<CN>\plugins) unless overridden.
function Resolve-InstallDir([string]$override, [string]$pluginHint) {
    if ($override) { return (Resolve-HomePath $override) }
    # Walk up from the resolved plugin dir to the directory literally named
    # 'GWToolboxpp' (…\Documents\GWToolboxpp) -- robust whether the plugin dir is
    # …\GWToolboxpp\<CN>\plugins or the flatter …\GWToolboxpp\plugins.
    $d = Resolve-PluginDir $pluginHint
    while ($d -and (Split-Path $d -Leaf) -ne 'GWToolboxpp') {
        $parent = Split-Path $d -Parent
        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $d) { $d = ''; break }
        $d = $parent
    }
    if ($d) { return $d }
    return (Join-Path $Profile2 'Documents\GWToolboxpp')
}
# A real PE image starts with 'MZ'. Cheap structural check for the exe + main dll
# (the plugin gets a stronger exported-symbol check via Test-Export).
function Test-IsPE([string]$path) {
    try {
        $fs = [System.IO.File]::OpenRead($path)
        try { $b0 = $fs.ReadByte(); $b1 = $fs.ReadByte() } finally { $fs.Dispose() }
        return ($b0 -eq 0x4D -and $b1 -eq 0x5A)
    } catch { return $false }
}
# Deploy one artifact robustly. Move-Item -Force first; if the target is a running exe
# or a loaded DLL its file is locked against delete -- but Windows still lets us RENAME
# it aside (the hot-patch trick GWToolbox's own updater uses), so fall back to
# rename-to-.old then move the new file in. Returns $true only on real success.
function Deploy-Artifact([string]$src, [string]$destDir, [string]$destName) {
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    $target = Join-Path $destDir $destName
    $tmp = "$target.new"
    $srcHash = (Get-FileHash $src -Algorithm SHA256).Hash
    # -ErrorAction Stop is REQUIRED: the worker runs $ErrorActionPreference='Continue', so a plain
    # Move-Item failure on a locked target is NON-terminating -- it slips past try/catch, skips the
    # rename-aside fallback, and still falls through to success. That is the silent no-op-on-lock.
    # Stop makes it throw so the fallback runs; the hash check below reports the truth regardless.
    Copy-Item $src $tmp -Force -ErrorAction Stop
    try {
        Move-Item $tmp $target -Force -ErrorAction Stop
    } catch {
        # Target is a running exe / loaded dll: locked against overwrite, but NTFS still lets us
        # RENAME it aside, then move the new file into the freed name.
        $old = "$target.old"
        Remove-Item $old -Force -ErrorAction SilentlyContinue
        try {
            if (Test-Path $target) { Move-Item $target $old -Force -ErrorAction Stop }
            Move-Item $tmp $target -Force -ErrorAction Stop
            Remove-Item $old -Force -ErrorAction SilentlyContinue   # best-effort; a loaded .old lingers until restart
        } catch {
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
            return $false
        }
    }
    # Never trust the move -- confirm the deployed bytes match the source, or report failure.
    if (-not (Test-Path $target)) { return $false }
    return ((Get-FileHash $target -Algorithm SHA256).Hash -eq $srcHash)
}
# The (target, artifact, verify-kind, deploy-dir) set to build/verify/deploy for a Kind.
function Get-BuildItems([string]$kind, [string]$config, [string]$gwtb, [string]$pluginHint, [string]$installOverride) {
    $binDir = Join-Path $gwtb (Join-Path 'bin' $config)
    if ($kind -eq 'launcher') {
        $install = Resolve-InstallDir $installOverride $pluginHint
        return @(
            @{ Target = 'GWToolboxdll'; Name = 'GWToolboxdll.dll'; Path = (Join-Path $binDir 'GWToolboxdll.dll'); Verify = 'pe'; DeployDir = $install },
            @{ Target = 'GWToolbox';    Name = 'GWToolbox.exe';    Path = (Join-Path $binDir 'GWToolbox.exe');    Verify = 'pe'; DeployDir = $install }
        )
    }
    if ($kind -eq 'guildlite') {
        $install = if ($installOverride) { Resolve-HomePath $installOverride } else { Join-Path $Profile2 'Documents\guildlite' }
        # Phase 1: guildlite.dll (monolith) + guildlite-inject.exe (loader).
        # Phase 2: guildlite-core.dll (reloadable) + guildlite-stub.dll (inject-once host).
        # gwca.dll is a prebuilt dependency staged next to the payloads by the CMake POST_BUILD;
        # its Target reuses 'guildlite' only to satisfy --target (cmake dedups the repeat).
        return @(
            @{ Target = 'guildlite';        Name = 'guildlite.dll';        Path = (Join-Path $binDir 'guildlite.dll');        Verify = 'pe'; DeployDir = $install },
            @{ Target = 'guildlite-inject'; Name = 'guildlite-inject.exe'; Path = (Join-Path $binDir 'guildlite-inject.exe'); Verify = 'pe'; DeployDir = $install },
            @{ Target = 'guildlite-core';   Name = 'guildlite-core.dll';   Path = (Join-Path $binDir 'guildlite-core.dll');   Verify = 'pe'; DeployDir = $install },
            @{ Target = 'guildlite-stub';   Name = 'guildlite-stub.dll';   Path = (Join-Path $binDir 'guildlite-stub.dll');   Verify = 'pe'; DeployDir = $install },
            @{ Target = 'guildlite';        Name = 'gwca.dll';             Path = (Join-Path $binDir 'gwca.dll');             Verify = 'pe'; DeployDir = $install }
        )
    }
    if ($kind -eq 'datcore') {
        # Portable Gw.dat extractor CLI. Standalone (no gwca/injection); the 32-bit
        # MSVC build compiles the real ATEX x86 asm (full-fidelity textures). Deploy
        # next to Gw.dat so it can be run in-place on the box.
        $install = if ($installOverride) { Resolve-HomePath $installOverride } else { Join-Path $Profile2 'Documents\guildlite' }
        # Deploy datcli.exe + armors.tsv (the provided name/profession/slot data) next to
        # Gw.dat so `datcli setup` finds everything in one dir on the box.
        return @(
            @{ Target = 'datcli'; Name = 'datcli.exe'; Path = (Join-Path $binDir 'datcli.exe');     Verify = 'pe';   DeployDir = $install },
            @{ Target = 'datcli'; Name = 'armors.tsv'; Path = (Join-Path $gwtb 'data\armors.tsv');  Verify = 'data'; DeployDir = $install }
        )
    }
    return @(
        @{ Target = 'Guildlite'; Name = 'Guildlite.dll'; Path = (Join-Path $binDir 'Guildlite.dll'); Verify = 'export:ToolboxPluginInstance'; DeployDir = (Resolve-PluginDir $pluginHint) }
    )
}
function Run-Dir([string]$id) { Join-Path (Join-Path $Profile2 '.guildlite') $id }
function Write-Marker([string]$dir, [string]$name, [string]$value) {
    Set-Content -Path (Join-Path $dir $name) -Value $value -Encoding ASCII -NoNewline
}
function Read-Marker([string]$dir, [string]$name) {
    $p = Join-Path $dir $name
    if (Test-Path $p) { return (Get-Content $p -Raw).Trim() } else { return '' }
}

# --- toolchain discovery (no dev shell / no inherited env needed) --------------
function Find-VSPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return '' }
    return (& $vswhere -latest -products * -property installationPath 2>$null | Select-Object -First 1)
}
function Find-CMake([string]$vsPath) {
    $c = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if ($c) { return $c }
    if ($vsPath) {
        $cand = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $cand) { return $cand }
    }
    return ''
}
function Resolve-VcpkgRoot([string]$override, [string]$vsPath) {
    if ($override) { return $override }
    if ($env:VCPKG_ROOT) { return $env:VCPKG_ROOT }
    if ($vsPath) { return (Join-Path $vsPath 'VC\vcpkg') }
    return ''
}
# Presence of the exported entry point proves GWToolbox's loader will accept the DLL.
# Byte-scan (Latin1 -> 1:1) avoids hunting for dumpbin in a non-dev-shell task context.
function Test-Export([string]$dll, [string]$name) {
    $bytes = [System.IO.File]::ReadAllBytes($dll)
    $text  = [System.Text.Encoding]::GetEncoding('ISO-8859-1').GetString($bytes)
    return ($text.IndexOf($name) -ge 0)
}

# =============================== DOCTOR ========================================
if ($Mode -eq 'Doctor') {
    $ok = $true
    $vsPath = Find-VSPath
    $cmake  = Find-CMake $vsPath
    $vcpkg  = Resolve-VcpkgRoot $VcpkgRoot $vsPath
    $srcRoot= Resolve-HomePath $SrcDir
    $plugin = Resolve-PluginDir $PluginDir

    if ($vsPath)               { "[doctor] Visual Studio: OK ($vsPath)" }        else { "[doctor] Visual Studio: MISSING (vswhere found nothing)"; $ok=$false }
    if ($cmake)                { "[doctor] cmake: OK ($cmake)" }                 else { "[doctor] cmake: MISSING (not on PATH, not in VS)"; $ok=$false }
    if ($vcpkg -and (Test-Path $vcpkg)) { "[doctor] vcpkg root: OK ($vcpkg)" }   else { "[doctor] vcpkg root: MISSING ($vcpkg)"; $ok=$false }
    if (Get-Command tar -ErrorAction SilentlyContinue) { "[doctor] tar: OK" }    else { "[doctor] tar: MISSING (need Win10 1803+ bsdtar)"; $ok=$false }
    if (Test-Path $srcRoot)    { "[doctor] source dir: OK ($srcRoot)" }          else { "[doctor] source dir: absent ($srcRoot) -- created on first push" }
    try { New-Item -ItemType Directory -Force -Path $plugin | Out-Null; "[doctor] plugin dir: OK ($plugin)" }
    catch { "[doctor] plugin dir: NOT WRITABLE ($plugin)"; $ok=$false }
    try {
        $drive = (Get-Item $Profile2).PSDrive
        $freeGB = [math]::Round($drive.Free/1GB,1)
        "[doctor] free disk: $freeGB GB on $($drive.Name):"
        if ($freeGB -lt 5) { "[doctor]   WARNING: low disk (<5GB), vcpkg builds are large" }
    } catch { "[doctor] free disk: unknown" }

    if ($ok) { "DOCTOR_RESULT=PASS" } else { "DOCTOR_RESULT=FAIL" }
    exit 0
}

# =============================== LAUNCH ========================================
# Register a detached Scheduled Task (owned by the Task Scheduler service, so it
# outlives the ssh session) and start it. All quoting is isolated into a run.cmd.
if ($Mode -eq 'Launch') {
    if (-not $RunId) { throw "Launch requires -RunId" }
    $runDir = Run-Dir $RunId
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    Write-Marker $runDir 'build.started' (Now-Epoch)

    $cleanArg    = if ($Clean)    { '-Clean' }    else { '' }
    $nodeployArg = if ($NoDeploy) { '-NoDeploy' } else { '' }
    $cmd = @"
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%USERPROFILE%\build_remote.ps1" -Mode Worker -RunId $RunId -Config $Config -SrcDir "$SrcDir" -PluginDir "$PluginDir" -Kind $Kind -InstallDir "$InstallDir" -Tarball "$Tarball" $cleanArg $nodeployArg
"@
    $runCmd = Join-Path $runDir 'run.cmd'
    Set-Content -Path $runCmd -Value $cmd -Encoding ASCII

    $taskName = "guildlite-build-$RunId"
    try {
        $action  = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c `"$runCmd`""
        $settings= New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Hours 2)
        Register-ScheduledTask -TaskName $taskName -Action $action -Settings $settings -Force | Out-Null
        Start-ScheduledTask -TaskName $taskName
    } catch {
        # Fallback for hosts without the ScheduledTasks module.
        schtasks /Create /F /TN $taskName /SC ONCE /ST 00:00 /TR "`"$runCmd`"" /RL LIMITED | Out-Null
        schtasks /Run /TN $taskName | Out-Null
    }
    "LAUNCHED $RunId"
    exit 0
}

# =============================== STATUS ========================================
if ($Mode -eq 'Status') {
    if (-not $RunId) { throw "Status requires -RunId" }
    $runDir = Run-Dir $RunId
    if (-not (Test-Path $runDir)) { "STATUS=UNKNOWN"; exit 0 }
    $exit = Read-Marker $runDir 'build.exitcode'
    if ($exit -ne '') {
        "STATUS=DONE"
        "EXITCODE=$exit"
        "VERIFY=$(Read-Marker $runDir 'build.verify')"
        "DEPLOYED=$(Read-Marker $runDir 'build.deployed')"
        "SHA256=$(Read-Marker $runDir 'build.sha256')"
        "DLL=$(Read-Marker $runDir 'build.dllpath')"
        "DLLMTIME=$(Read-Marker $runDir 'build.dllmtime')"
        "KIND=$(Read-Marker $runDir 'build.kind')"
        "STARTED=$(Read-Marker $runDir 'build.started')"
        $manifest = Join-Path $runDir 'build.manifest'
        if (Test-Path $manifest) {
            foreach ($line in (Get-Content $manifest)) { if ($line -match '\S') { "FILE=$line" } }
        }
    } else {
        "STATUS=RUNNING"
        "STARTED=$(Read-Marker $runDir 'build.started')"
        $log = Join-Path $runDir 'build.log'
        if (Test-Path $log) {
            $last = Get-Content $log -Tail 8 | Where-Object { $_ -match '\S' } | Select-Object -Last 1
            if ($last) { "LASTLINE=$($last.Trim())" }
        }
    }
    exit 0
}

# =============================== WORKER ========================================
if (-not $RunId) { $RunId = "local-" + (Get-Date -Format 'yyyyMMdd-HHmmss') }
$runDir = Run-Dir $RunId
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$log = Join-Path $runDir 'build.log'
if (-not (Test-Path (Join-Path $runDir 'build.started'))) { Write-Marker $runDir 'build.started' (Now-Epoch) }
$started = [int](Read-Marker $runDir 'build.started')
function Log([string]$m) { $line = "[$(Get-Date -Format HH:mm:ss)] $m"; Add-Content -Path $log -Value $line }

$exitCode = 1
try {
    $vsPath  = Find-VSPath
    $cmake   = Find-CMake $vsPath
    if (-not $cmake) { throw "cmake not found (VS path: '$vsPath')" }
    $vcpkg   = Resolve-VcpkgRoot $VcpkgRoot $vsPath
    if (-not $vcpkg) { throw "VCPKG_ROOT not resolvable" }
    $env:VCPKG_ROOT = $vcpkg
    $srcRoot = Resolve-HomePath $SrcDir
    # plugin/launcher build the GWToolboxpp tree; guildlite builds our standalone injector/
    # tree; datcore builds the portable Gw.dat extractor CLI (datcli.exe).
    $gwtb    = if ($Kind -eq 'guildlite') { Join-Path $srcRoot 'injector' }
               elseif ($Kind -eq 'datcore') { Join-Path $srcRoot 'datcore' }
               else { Join-Path $srcRoot 'GWToolboxpp' }

    Log "run $RunId  config=$Config  cmake=$cmake  vcpkg=$vcpkg"

    # 1. extract the pushed working tree over any existing tree (keeps build/ + vcpkg_installed/).
    if ($Tarball) {
        $tp = Resolve-HomePath $Tarball
        if (Test-Path $tp) {
            New-Item -ItemType Directory -Force -Path $srcRoot | Out-Null
            Log "extracting $tp -> $srcRoot"
            & tar -xzf $tp -C $srcRoot 2>&1 | Out-File -FilePath $log -Append -Encoding utf8
            if ($LASTEXITCODE -ne 0) { throw "tar extraction failed ($LASTEXITCODE)" }
        } else { Log "WARNING: tarball $tp not found; building existing tree" }
    }
    if (-not (Test-Path $gwtb)) { throw "project source not found at $gwtb (Kind=$Kind)" }
    Set-Location $gwtb

    # 2. optional cache wipe (surgical: keeps vcpkg_installed so deps aren't rebuilt).
    if ($Clean) {
        Log "clean: wiping build\CMakeCache.txt + build\CMakeFiles"
        Remove-Item (Join-Path $gwtb 'build\CMakeCache.txt') -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $gwtb 'build\CMakeFiles') -Recurse -Force -ErrorAction SilentlyContinue
    }

    # 3. configure (retry once on a stale cache -- the documented ~50% failure mode).
    Log "configure: cmake --preset vcpkg"
    & $cmake --preset vcpkg 2>&1 | Out-File -FilePath $log -Append -Encoding utf8
    if ($LASTEXITCODE -ne 0 -and -not $Clean) {
        Log "configure failed ($LASTEXITCODE) -- wiping cache and retrying once"
        Remove-Item (Join-Path $gwtb 'build\CMakeCache.txt') -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $gwtb 'build\CMakeFiles') -Recurse -Force -ErrorAction SilentlyContinue
        & $cmake --preset vcpkg 2>&1 | Out-File -FilePath $log -Append -Encoding utf8
    }
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

    # 4. build the target(s) for this Kind, all cores. plugin -> Guildlite; launcher ->
    #    GWToolboxdll (main injected DLL) + GWToolbox.exe (the v4.7 launcher, updater neutered).
    Write-Marker $runDir 'build.kind' $Kind
    $items = Get-BuildItems $Kind $Config $gwtb $PluginDir $InstallDir
    $targetArgs = @(); foreach ($it in $items) { $targetArgs += @('--target', $it.Target) }
    Log "build: cmake --build build --config $Config --parallel $($targetArgs -join ' ')"
    & $cmake --build build --config $Config --parallel @targetArgs 2>&1 | Out-File -FilePath $log -Append -Encoding utf8
    $exitCode = $LASTEXITCODE
    Log "cmake --build exit=$exitCode"

    # 5. verify BEFORE accepting: cmake exit 0 + each artifact exists + passes its check
    #    (plugin exports the entry point; exe/main-dll are valid PE images). Copy each
    #    accepted artifact (+ pdb) into the run dir, hash it, and record a manifest line.
    $verify = 'FAIL:unknown'
    $manifestLines = @()
    if ($exitCode -ne 0) {
        $verify = "FAIL:build-exit-$exitCode"
    } else {
        $verify = 'PASS'
        foreach ($it in $items) {
            if (-not (Test-Path $it.Path)) { $verify = "FAIL:missing-$($it.Name)"; break }
            if ($it.Verify -like 'export:*') {
                if (-not (Test-Export $it.Path ($it.Verify.Substring(7)))) { $verify = "FAIL:no-export-$($it.Name)"; break }
            } elseif ($it.Verify -eq 'pe' -and -not (Test-IsPE $it.Path)) {
                $verify = "FAIL:not-pe-$($it.Name)"; break
            }
            $mtime = [DateTimeOffset]::new((Get-Item $it.Path).LastWriteTimeUtc).ToUnixTimeSeconds()
            if ($mtime -lt $started) { Log "note: $($it.Name) unchanged since last build (incremental no-op)" }
            Copy-Item $it.Path (Join-Path $runDir $it.Name) -Force
            $pdb = [System.IO.Path]::ChangeExtension($it.Path, 'pdb')
            if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $runDir ([System.IO.Path]::GetFileName($pdb))) -Force }
            $hash = (Get-FileHash $it.Path -Algorithm SHA256).Hash.ToLower()
            $manifestLines += "$($it.Name) $hash $($it.DeployDir)"
            if ($manifestLines.Count -eq 1) {
                Write-Marker $runDir 'build.sha256'   $hash
                Write-Marker $runDir 'build.dllpath'  $it.Path
                Write-Marker $runDir 'build.dllmtime' "$mtime"
            }
            Log "verify: $($it.Name) OK  sha256=$hash"
        }
    }
    if ($manifestLines.Count -gt 0) {
        Set-Content -Path (Join-Path $runDir 'build.manifest') -Value ($manifestLines -join "`n") -Encoding ASCII
    }
    Write-Marker $runDir 'build.verify' $verify
    if ($verify -ne 'PASS') { Log "verify: $verify" }

    # 6. deploy each accepted artifact where GW / the launcher loads it (Windows-local).
    #    Deploy-Artifact dodges the sharing violation from a running exe / loaded dll.
    $deployed = '0'
    if ($verify -eq 'PASS' -and -not $NoDeploy) {
        $allok = $true
        foreach ($it in $items) {
            if (Deploy-Artifact $it.Path $it.DeployDir $it.Name) {
                Log "deployed -> $(Join-Path $it.DeployDir $it.Name)"
            } else {
                $allok = $false
                Log "deploy FAILED for $($it.Name) (locked? is GW / GWToolbox running and holding it?)"
            }
        }
        if ($allok) { $deployed = '1' }
    }
    Write-Marker $runDir 'build.deployed' $deployed
}
catch {
    Log "ERROR: $($_.Exception.Message)"
    if ($exitCode -eq 0) { $exitCode = 1 }
    if (-not (Test-Path (Join-Path $runDir 'build.verify'))) { Write-Marker $runDir 'build.verify' "FAIL:$($_.Exception.Message)" }
    if (-not (Test-Path (Join-Path $runDir 'build.deployed'))) { Write-Marker $runDir 'build.deployed' '0' }
}
finally {
    # exit-code marker is written LAST and atomically: absence=RUNNING, presence=DONE.
    $tmp = Join-Path $runDir 'build.exitcode.tmp'
    Set-Content -Path $tmp -Value "$exitCode" -Encoding ASCII -NoNewline
    Move-Item $tmp (Join-Path $runDir 'build.exitcode') -Force
    if ($RunId -notlike 'local-*') {
        try { Unregister-ScheduledTask -TaskName "guildlite-build-$RunId" -Confirm:$false -ErrorAction SilentlyContinue } catch {}
    }
}
exit $exitCode
