#!/usr/bin/env bash
# ------------------------------------------------------------------------------
# gwt.sh -- Guildlite <-> GWToolbox audit harness over SSH.
# ------------------------------------------------------------------------------
# A thin shim onto the live Windows GW client (`ssh bob@bobmobile.local`) for
# planning, driving and VERIFYING model exports without sitting at the machine:
#
#   gwt.sh state                 print the newest export manifest (our own JSON
#                                game-state log == the "read state over SSH" shim)
#   gwt.sh ls [n]                list the n newest export files on the box
#   gwt.sh fetch [destdir]       pull the newest export set (obj/mtl/tga/json) back
#   gwt.sh shot [out.png]        screen-capture the GW client window, pull it back
#   gwt.sh cmd "/armory ..."     drive a slash command into GW chat (SendKeys)
#   gwt.sh keys "text"           send raw keystrokes to the GW window
#   gwt.sh render [out.png]      fetch newest .obj + render it locally to a .png
#   gwt.sh loop "/armory AXE"    cmd -> wait -> shot: one turn of the audit loop
#
# Config (env, or ~/etc/term/windows-env.sh): GUILDLITE_REMOTE (ssh target),
# GUILDLITE_WIN_EXPORT (remote export dir, relative to %USERPROFILE%).
# ------------------------------------------------------------------------------
set -euo pipefail

[ -f "$HOME/etc/term/windows-env.sh" ] && . "$HOME/etc/term/windows-env.sh"
HOST="${GUILDLITE_REMOTE:-bob@bobmobile.local}"
WIN_EXPORT="${GUILDLITE_WIN_EXPORT:-Documents/GWToolboxpp/BOBMOBILE/guildlite}"
SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")" && pwd -P)"
SSH_OPTS=(-o ConnectTimeout=8 -o BatchMode=yes)

log() { printf '[gwt] %s\n' "$*" >&2; }
die() { printf '[gwt] error: %s\n' "$*" >&2; exit 1; }

# Run a PowerShell script (read from stdin) on the remote, robustly, by shipping
# it as a UTF-16LE base64 -EncodedCommand -- sidesteps all cmd/ssh quoting.
ps_remote() {
    local b64
    # Prepend a progress-silencer so remote PowerShell doesn't spew CLIXML on stderr.
    b64="$( { printf "%s\n" "\$ProgressPreference='SilentlyContinue'"; cat; } | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')"
    ssh "${SSH_OPTS[@]}" "$HOST" "powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand $b64"
}

# Absolute remote export dir, expanded against the remote profile.
remote_export_expr='(Join-Path $env:USERPROFILE "'"$WIN_EXPORT"'")'

cmd_ls() {
    local n="${1:-12}"
    ps_remote <<PS
\$d = $remote_export_expr
if (!(Test-Path \$d)) { Write-Output "(no export dir yet: \$d)"; exit 0 }
Get-ChildItem \$d -File | Sort-Object LastWriteTime -Descending |
  Select-Object -First $n |
  ForEach-Object { "{0,-40} {1,10} {2}" -f \$_.Name, \$_.Length, \$_.LastWriteTime }
PS
}

# Echo the stem (basename without extension) of the newest .obj, or .json.
newest_stem() {
    ps_remote <<PS
\$d = $remote_export_expr
\$f = Get-ChildItem \$d -File -Include *.obj,*.stl,*.json -ErrorAction SilentlyContinue |
      Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (\$f) { [System.IO.Path]::GetFileNameWithoutExtension(\$f.Name) }
PS
}

cmd_state() {
    ps_remote <<PS
\$d = $remote_export_expr
\$f = Get-ChildItem \$d -File -Filter *.json -ErrorAction SilentlyContinue |
      Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (\$f) { Get-Content \$f.FullName -Raw } else { Write-Output "(no manifest found in \$d)" }
PS
}

cmd_fetch() {
    local dest="${1:-./guildlite-exports}"
    mkdir -p "$dest"
    local stem
    stem="$(newest_stem | tr -d '\r')"
    [ -n "$stem" ] || die "no export set found on $HOST:$WIN_EXPORT"
    log "newest set: $stem -> $dest"
    # Forward-slash Windows paths work for scp; profile is C:/Users/<user>.
    local home_win
    home_win="$(ssh "${SSH_OPTS[@]}" "$HOST" 'powershell -NoProfile -Command "$env:USERPROFILE -replace \"\\\\\",\"/\""' | tr -d '\r')"
    scp "${SSH_OPTS[@]}" "$HOST:$home_win/$WIN_EXPORT/$stem.*" "$dest/" 2>/dev/null || \
        die "scp failed (nothing matching $stem.*?)"
    log "fetched: $(ls "$dest/$stem".* 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
    echo "$dest/$stem.obj"
}

cmd_shot() {
    local out="${1:-./guildlite-shot.png}"
    log "capturing GW client window on $HOST ..."
    ps_remote <<'PS' || true
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;using System.Runtime.InteropServices;
public class W{
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 public struct R{public int L,T,Rt,B;}
}
"@
$tmp = Join-Path $env:TEMP "guildlite_shot.png"
$p = Get-Process Gw -ErrorAction SilentlyContinue | Select-Object -First 1
$h = if ($p) { $p.MainWindowHandle } else { [IntPtr]::Zero }
if ($h -ne [IntPtr]::Zero) { [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 400 }
$r = New-Object 'W+R'
if ($h -ne [IntPtr]::Zero -and [W]::GetWindowRect($h,[ref]$r) -and ($r.Rt-$r.L) -gt 0) {
  $x=$r.L; $y=$r.T; $w=$r.Rt-$r.L; $ht=$r.B-$r.T
} else {
  $b=[System.Windows.Forms.SystemInformation]::VirtualScreen; $x=$b.X; $y=$b.Y; $w=$b.Width; $ht=$b.Height
}
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
$bmp.Save($tmp,[System.Drawing.Imaging.ImageFormat]::Png)
Write-Output $tmp
PS
    local home_win
    home_win="$(ssh "${SSH_OPTS[@]}" "$HOST" 'powershell -NoProfile -Command "$env:TEMP -replace \"\\\\\",\"/\""' | tr -d '\r')"
    scp "${SSH_OPTS[@]}" "$HOST:$home_win/guildlite_shot.png" "$out" >/dev/null 2>&1 || die "could not fetch screenshot"
    log "screenshot -> $out"
    echo "$out"
}

# SendKeys the given text to the GW window (special chars +^%~(){} are literalised).
send_keys() {
    local text="$1"
    ps_remote <<PS || true
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;using System.Runtime.InteropServices;
public class F{[DllImport("user32.dll")] public static extern IntPtr FindWindow(string c,string n);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);}
"@
\$h = [F]::FindWindow(\$null,"Guild Wars")
if (\$h -eq [IntPtr]::Zero) { Write-Error "GW window not found"; exit 1 }
[F]::SetForegroundWindow(\$h) | Out-Null; Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait(@'
$text
'@)
PS
}

cmd_cmd() {
    local slash="$1"
    [ -n "$slash" ] || die "usage: gwt.sh cmd \"/armory ...\""
    log "driving into GW chat: $slash"
    send_keys "{ENTER}"; sleep 0.3
    send_keys "$slash"; sleep 0.2
    send_keys "{ENTER}"
    log "sent."
}

cmd_render() {
    local out="${1:-./guildlite-render.png}"
    local obj
    obj="$(cmd_fetch ./guildlite-exports | tail -n1)"
    [ -f "$obj" ] || die "no .obj fetched to render"
    python3 "$SELF_DIR/obj_render.py" "$obj" "$out"
}

cmd_loop() {
    local slash="$1"
    cmd_cmd "$slash"
    log "waiting 2.5s for redraw ..."
    sleep 2.5
    cmd_shot "./guildlite-loop.png"
}

main() {
    local sub="${1:-help}"; shift || true
    case "$sub" in
        state)  cmd_state ;;
        ls)     cmd_ls "${1:-12}" ;;
        fetch)  cmd_fetch "${1:-}" ;;
        shot|screenshot) cmd_shot "${1:-}" ;;
        cmd)    cmd_cmd "${1:-}" ;;
        keys)   send_keys "${1:-}" ;;
        render) cmd_render "${1:-}" ;;
        loop)   cmd_loop "${1:-}" ;;
        help|-h|--help)
            sed -n '2,30p' "$0" ;;
        *) die "unknown subcommand: $sub (try: gwt.sh help)" ;;
    esac
}

main "$@"
