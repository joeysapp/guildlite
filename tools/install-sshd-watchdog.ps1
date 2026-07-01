<#
  install-sshd-watchdog.ps1 -- one-time ELEVATED setup of a self-healing sshd watchdog.

  The Windows OpenSSH sshd on this box wedges under load (accepts a connection, then
  resets the session at channel-open) and needs a manual admin `Restart-Service sshd`.
  This registers a SYSTEM scheduled task that every few minutes does a FUNCTIONAL probe
  (a real auth + session to localhost, using a dedicated key) and restarts sshd only when
  that session actually fails -- so the wedge self-heals without a manual admin prompt.

  Safe by design: it validates the probe once during install and REFUSES to enable the
  task if the probe can't succeed, so it can never fall into a false-positive restart loop.

  Run from an ADMINISTRATOR PowerShell:
    powershell -ExecutionPolicy Bypass -File install-sshd-watchdog.ps1
  Remove with:  Unregister-ScheduledTask -TaskName guildlite-sshd-watchdog -Confirm:$false
#>
[CmdletBinding()]
param([int]$IntervalMinutes = 3, [string]$ProbeUser = $env:USERNAME)

$ErrorActionPreference = 'Stop'
function Fail($m) { Write-Host "[watchdog-install] ERROR: $m" -ForegroundColor Red; exit 1 }

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        ).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
    Fail "run this from an elevated (Administrator) PowerShell (Win+X -> Terminal (Admin))."
}
if (-not (Get-Service sshd -ErrorAction SilentlyContinue)) { Fail "the OpenSSH 'sshd' service is not installed." }

$dir = 'C:\ProgramData\guildlite'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$key      = Join-Path $dir 'sshd_watchdog_ed25519'
$watchdog = Join-Path $dir 'sshd-watchdog.ps1'
$known    = Join-Path $dir 'known_hosts'

# 1. dedicated passwordless probe key (cmd wrapper so the empty passphrase survives).
if (-not (Test-Path $key)) {
    cmd /c "ssh-keygen -t ed25519 -f `"$key`" -N `"`" -C guildlite-sshd-watchdog" | Out-Null
    if (-not (Test-Path $key)) { Fail "ssh-keygen failed to create $key" }
}
$pub = (Get-Content "$key.pub" -Raw).Trim()

# 2. authorize it for local logins. An admin user authenticates via
#    administrators_authorized_keys, which sshd requires to be owned by SYSTEM/Admins only.
$aak = 'C:\ProgramData\ssh\administrators_authorized_keys'
if (-not (Test-Path $aak)) { New-Item -ItemType File -Force -Path $aak | Out-Null }
if (-not (Select-String -Path $aak -SimpleMatch $pub -Quiet)) { Add-Content -Path $aak -Value $pub }
icacls $aak /inheritance:r /grant 'SYSTEM:F' 'BUILTIN\Administrators:F' | Out-Null

# 3. write the watchdog script (probes ProbeUser@localhost; restarts sshd only on real failure).
$body = @"
`$ErrorActionPreference = 'Continue'
`$log = 'C:\ProgramData\guildlite\sshd-watchdog.log'
function L(`$m){ Add-Content `$log ('[{0}] {1}' -f (Get-Date -Format s), `$m) }
`$svc = Get-Service sshd -ErrorAction SilentlyContinue
if (-not `$svc) { L 'sshd service missing'; exit }
if (`$svc.Status -ne 'Running') { L 'sshd not running -> Start-Service'; Start-Service sshd; exit }
# functional probe: a real auth+session. A wedged sshd resets the session here.
& ssh -i '$key' -o BatchMode=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=accept-new ``
      -o UserKnownHostsFile='$known' '$ProbeUser@localhost' 'exit 0' 2>`$null
if (`$LASTEXITCODE -ne 0) { L ('probe failed rc=' + `$LASTEXITCODE + ' -> Restart-Service sshd'); Restart-Service sshd -Force; L 'restarted' }
"@
Set-Content -Path $watchdog -Value $body -Encoding ASCII

# 4. VALIDATE the probe before enabling anything -- refuse to install a false-positive loop.
Write-Host "[watchdog-install] validating functional probe ($ProbeUser@localhost) ..."
& ssh -i $key -o BatchMode=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=accept-new `
      -o UserKnownHostsFile=$known "$ProbeUser@localhost" "exit 0" 2>$null
if ($LASTEXITCODE -ne 0) {
    Fail "probe login failed (rc=$LASTEXITCODE). Not installing the task (would restart-loop). Check that '$ProbeUser' is an admin and administrators_authorized_keys ACLs are correct."
}
Write-Host "[watchdog-install] probe OK."

# 5. register the SYSTEM task (every N minutes).
$action    = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$watchdog`""
$trigger   = New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes $IntervalMinutes)
$principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName 'guildlite-sshd-watchdog' -Action $action -Trigger $trigger -Principal $principal -Force | Out-Null
Write-Host "[watchdog-install] installed 'guildlite-sshd-watchdog' (every $IntervalMinutes min). Log: $dir\sshd-watchdog.log" -ForegroundColor Green
