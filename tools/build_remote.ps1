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
powershell -NoProfile -ExecutionPolicy Bypass -File "%USERPROFILE%\build_remote.ps1" -Mode Worker -RunId $RunId -Config $Config -SrcDir "$SrcDir" -PluginDir "$PluginDir" -Tarball "$Tarball" $cleanArg $nodeployArg
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
        "STARTED=$(Read-Marker $runDir 'build.started')"
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
    $gwtb    = Join-Path $srcRoot 'GWToolboxpp'

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
    if (-not (Test-Path $gwtb)) { throw "GWToolboxpp not found at $gwtb" }
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

    # 4. build just the plugin, all cores.
    Log "build: cmake --build build --config $Config --parallel --target Guildlite"
    & $cmake --build build --config $Config --parallel --target Guildlite 2>&1 | Out-File -FilePath $log -Append -Encoding utf8
    $exitCode = $LASTEXITCODE
    Log "cmake --build exit=$exitCode"

    # 5. verify BEFORE accepting: cmake exit 0 + DLL exists + exports the entry point.
    $dll = Join-Path $gwtb (Join-Path 'bin' (Join-Path $Config 'Guildlite.dll'))
    $verify = 'FAIL:unknown'
    if ($exitCode -eq 0) {
        if (-not (Test-Path $dll)) {
            $verify = 'FAIL:dll-missing'
        } elseif (-not (Test-Export $dll 'ToolboxPluginInstance')) {
            $verify = 'FAIL:no-export'
        } else {
            $verify = 'PASS'
            $mtime = [DateTimeOffset]::new((Get-Item $dll).LastWriteTimeUtc).ToUnixTimeSeconds()
            Write-Marker $runDir 'build.dllmtime' "$mtime"
            Write-Marker $runDir 'build.dllpath'  "$dll"
            # mtime < build start just means an incremental no-op (nothing changed);
            # cmake exit 0 already proves the DLL is current, so note it -- don't fail.
            if ($mtime -lt $started) { Log "note: DLL unchanged since last build (incremental no-op)" }
            Copy-Item $dll (Join-Path $runDir 'Guildlite.dll') -Force
            $pdb = [System.IO.Path]::ChangeExtension($dll, 'pdb')
            if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $runDir 'Guildlite.pdb') -Force }
            $hash = (Get-FileHash $dll -Algorithm SHA256).Hash.ToLower()
            Write-Marker $runDir 'build.sha256' $hash
            Log "verify: PASS  sha256=$hash"
        }
    } else {
        $verify = "FAIL:build-exit-$exitCode"
    }
    Write-Marker $runDir 'build.verify' $verify
    if ($verify -ne 'PASS') { Log "verify: $verify" }

    # 6. deploy (Windows-local copy where GW loads it). .new + rename dodges the
    #    sharing-violation when GWToolbox has the old DLL locked.
    $deployed = '0'
    if ($verify -eq 'PASS' -and -not $NoDeploy) {
        $plugin = Resolve-PluginDir $PluginDir
        New-Item -ItemType Directory -Force -Path $plugin | Out-Null
        $target = Join-Path $plugin 'Guildlite.dll'
        $tmp    = "$target.new"
        try {
            Copy-Item $dll $tmp -Force
            Move-Item $tmp $target -Force
            $deployed = '1'
            Log "deployed -> $target"
        } catch {
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
            Log "deploy FAILED (is GWToolbox running and holding Guildlite.dll?): $($_.Exception.Message)"
        }
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
