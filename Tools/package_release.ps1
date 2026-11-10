# Release package of DimensionalSinkingDepot: Win64 client (Steam + EGS), Win64 and Linux servers. Package only:
# nothing is copied into a game or server folder.
#
# Usage (Windows PowerShell 5.1):
#   powershell -NoProfile -ExecutionPolicy Bypass -File package_release.ps1 [-Log <path>] [-Engine <engine root>]
# Success: the last log line is "RESULT: OK". Failure: "RESULT: FAILED <reason>" (exit code 1).
# UAT's own output goes to <Log>.uat.txt next to the log.
#
# Checks:
#   before: the dev commands are off in DimensionalSinkingDepot.Build.cs; no UBT/UAT is running.
#   after:  the three zips exist and hold the expected binaries; no dev command strings in any shipped binary;
#           size and SHA-256 of each zip.
param(
  [string]$Log = (Join-Path $PSScriptRoot '..\Saved\package_release.log'),
  [string]$Engine = 'M:\Program Files\Unreal Engine - CSS\Engine'
)
$ErrorActionPreference = 'Stop'

# Logging first, so every later failure is recorded. Append-only, shared, so a progress window can read along.
$Log = [IO.Path]::GetFullPath($Log)
New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null
function Write-Log([string]$Line) {
  $stamped = "[{0:HH:mm:ss}] {1}" -f (Get-Date), $Line
  $fs = [IO.File]::Open($Log, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
  try { $bytes = [Text.Encoding]::UTF8.GetBytes($stamped + "`r`n"); $fs.Write($bytes, 0, $bytes.Length) } finally { $fs.Dispose() }
  Write-Host $stamped
}
function Fail([string]$Reason) { Write-Log "RESULT: FAILED $Reason"; exit 1 }

try {
  $ModRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
  $ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ModRoot '..\..'))
  $UProject = Join-Path $ProjectRoot 'FactoryGame.uproject'
  $Archive = Join-Path $ProjectRoot 'Saved\ArchivedPlugins\DimensionalSinkingDepot'
  $UatLog = "$Log.uat.txt"
  Write-Log "package_release start: project $ProjectRoot, engine $Engine"

  # 1. Release configuration.
  $buildCs = Get-Content (Join-Path $ModRoot 'Source\DimensionalSinkingDepot\DimensionalSinkingDepot.Build.cs') -Raw
  if ($buildCs -notmatch 'const bool bDevCommands = false;') { Fail 'bDevCommands is not false in DimensionalSinkingDepot.Build.cs' }
  if (-not (Test-Path $UProject)) { Fail "no $UProject" }
  $busy = @(Get-CimInstance Win32_Process -Filter "Name='dotnet.exe'" | Where-Object { $_.CommandLine -match 'UnrealBuildTool\.dll|AutomationTool\.dll' })
  if ($busy.Count -gt 0) { Fail 'another UBT/UAT is running' }
  Write-Log 'step 1 ok: dev commands off, no other build running'

  # 2. Environment: Linux cross toolchain, temp on M: (keeps the Windows username out of PDB paths).
  $env:LINUX_MULTIARCH_ROOT = [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'Machine')
  if (-not $env:LINUX_MULTIARCH_ROOT) { Fail 'LINUX_MULTIARCH_ROOT (machine) is not set' }
  $env:TEMP = 'M:\Satismod\BuildTemp'; $env:TMP = 'M:\Satismod\BuildTemp'
  New-Item -ItemType Directory -Force $env:TEMP | Out-Null

  # 3. Engine PCH pre-read (cold-cache stall otherwise), right before UAT and again once cl.exe has started.
  $pchs = @(Get-ChildItem "$ProjectRoot\Intermediate\Build\Win64\x64\FactoryGame*\Shipping\Engine\SharedPCH.Engine.*.pch", "$ProjectRoot\Intermediate\Build\Win64\x64\FactoryServer\Shipping\Engine\SharedPCH.Engine.*.pch" -ErrorAction SilentlyContinue)
  function Read-Pchs {
    foreach ($p in $pchs) {
      try {
        $fs = [IO.File]::Open($p.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        try { $buf = New-Object byte[] (8MB); while ($fs.Read($buf, 0, $buf.Length) -gt 0) { } } finally { $fs.Dispose() }
      } catch { Write-Log "PCH pre-read skipped for $($p.Name): $($_.Exception.Message)" }
    }
  }
  Read-Pchs
  Write-Log "step 3 ok: pre-read $($pchs.Count) PCH file(s)"

  # 4. UAT PackagePlugin.
  $before = @{}; Get-ChildItem $Archive -Filter '*.zip' -ErrorAction SilentlyContinue | ForEach-Object { $before[$_.Name] = $_.LastWriteTimeUtc }
  $uatArgs = @("-ScriptsForProject=$UProject", 'PackagePlugin', "-project=$UProject", '-clientconfig=Shipping', '-serverconfig=Shipping',
    '-utf8output', '-DLCName=DimensionalSinkingDepot', '-build', '-platform=Win64', '-server', '-serverplatform=Win64+Linux',
    '-nocompileeditor', '-installed', '-WaitForUATMutex')
  Write-Log "step 4: RunUAT PackagePlugin (UAT log: $UatLog)"
  $uat = Start-Process -FilePath "$Engine\Build\BatchFiles\RunUAT.bat" -ArgumentList $uatArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $UatLog -RedirectStandardError "$UatLog.err"
  $reread = $false
  while (-not $uat.HasExited) {
    if (-not $reread -and (Get-Process cl -ErrorAction SilentlyContinue)) { Read-Pchs; $reread = $true; Write-Log 'PCHs re-read at cl.exe start' }
    Start-Sleep -Seconds 5
  }
  if (-not (Select-String -Path $UatLog -Pattern 'BUILD SUCCESSFUL' -Quiet)) {
    Select-String -Path $UatLog -Pattern 'error C\d+|error LNK|: error|BUILD FAILED|ConflictingInstance' | Select-Object -First 10 | ForEach-Object { Write-Log "  $($_.Line.Trim())" }
    Fail 'UAT did not report BUILD SUCCESSFUL'
  }
  Write-Log 'step 4 ok: BUILD SUCCESSFUL'

  # 5. Outputs: fresh zips with the expected binaries.
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $expected = @{
    'DimensionalSinkingDepot-Windows.zip'       = @('Binaries/Win64/FactoryGameSteam-DimensionalSinkingDepot-Win64-Shipping.dll', 'Binaries/Win64/FactoryGameEGS-DimensionalSinkingDepot-Win64-Shipping.dll', 'Content/Paks/Windows/DimensionalSinkingDepotFactoryGame-Windows.pak', 'DimensionalSinkingDepot.uplugin')
    'DimensionalSinkingDepot-WindowsServer.zip' = @('Binaries/Win64/FactoryServer-DimensionalSinkingDepot-Win64-Shipping.dll', 'DimensionalSinkingDepot.uplugin')
    'DimensionalSinkingDepot-LinuxServer.zip'   = @('Binaries/Linux/libFactoryServer-DimensionalSinkingDepot-Linux-Shipping.so', 'DimensionalSinkingDepot.uplugin')
  }
  # Dev-only strings; none may appear in a release binary (ASCII or UTF-16, as console names and log text are wide).
  $devMarkers = @('SinkingDepot.Dev.', 'SinkingDepotDevBuild', 'SinkingDepotMenuCheck', 'Development commands are compiled in')
  foreach ($name in $expected.Keys) {
    $zipPath = Join-Path $Archive $name
    if (-not (Test-Path $zipPath)) { Fail "missing $zipPath" }
    $item = Get-Item $zipPath
    if ($before.ContainsKey($name) -and $before[$name] -eq $item.LastWriteTimeUtc) { Fail "$name was not rebuilt" }
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
      $entries = @($zip.Entries | ForEach-Object { $_.FullName })
      foreach ($want in $expected[$name]) { if ($entries -notcontains $want) { Fail "$name lacks $want" } }
      foreach ($entry in $zip.Entries | Where-Object { $_.FullName -match '\.(dll|so)$' }) {
        $ms = New-Object IO.MemoryStream; $s = $entry.Open(); try { $s.CopyTo($ms) } finally { $s.Dispose() }
        $bytes = $ms.ToArray(); $ascii = [Text.Encoding]::ASCII.GetString($bytes); $wide = [Text.Encoding]::Unicode.GetString($bytes)
        $wideOdd = [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1)
        foreach ($m in $devMarkers) { if ($ascii.Contains($m) -or $wide.Contains($m) -or $wideOdd.Contains($m)) { Fail "$name/$($entry.FullName) contains dev string '$m'" } }
      }
    } finally { $zip.Dispose() }
    $hash = (Get-FileHash $zipPath -Algorithm SHA256).Hash
    Write-Log ("output {0}: {1:N0} bytes, SHA-256 {2}" -f $zipPath, $item.Length, $hash)
  }
  Write-Log 'step 5 ok: zips present, expected binaries inside, no dev strings'
  Write-Log 'RESULT: OK'
  exit 0
}
catch {
  Fail "exception: $($_.Exception.Message) at line $($_.InvocationInfo.ScriptLineNumber)"
}
