[CmdletBinding()]
param(
  [string]$RepoRoot,
  [string]$Preset = "windows-msvc-debug",
  [string]$ConfigPath = "config/pipeline.windows.toml",
  [string]$VcpkgRoot,
  [string]$OnnxRuntimeRoot,
  [switch]$BuildFirst,
  [string[]]$AppArgs = @(),
  [int]$TimeoutSeconds = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-MevRepoRoot -RepoRoot $RepoRoot
Assert-MevWindowsCheckout -RepoRoot $repoRoot

[void](Set-MevBuildEnvironment -RepoRoot $repoRoot -VcpkgRoot $VcpkgRoot -OnnxRuntimeRoot $OnnxRuntimeRoot)

if ($BuildFirst) {
  & (Join-Path $PSScriptRoot "build.ps1") -RepoRoot $repoRoot -Preset $Preset -VcpkgRoot $env:VCPKG_ROOT -OnnxRuntimeRoot $env:ONNXRUNTIME_ROOT
  if ($LASTEXITCODE -ne 0) {
    throw "Build failed before running the application."
  }
}

# whisper/ggml shared DLLs land in build/<preset>/bin.  Prepend that
# directory to PATH so mev_voice_mic.exe can resolve them at startup.
$buildBinDir = Join-Path $repoRoot "build" $Preset "bin"
if (Test-Path $buildBinDir) {
  $env:PATH = "$buildBinDir;$env:PATH"
  Write-Host "==> DLL search path: $buildBinDir"
}

$exePath = Get-MevExecutablePath -RepoRoot $repoRoot -Preset $Preset
if (-not (Test-Path -Path $exePath)) {
  throw "Executable not found: $exePath. Run build.ps1 first or use -BuildFirst."
}

$resolvedConfigPath = Resolve-MevConfigPath -RepoRoot $repoRoot -ConfigPath $ConfigPath

if (-not (Test-Path -Path $resolvedConfigPath)) {
  throw "Config file not found: $resolvedConfigPath"
}

$arguments = @("--config", $resolvedConfigPath)
if ($AppArgs.Count -gt 0) {
  $arguments += $AppArgs
}

if ($TimeoutSeconds -gt 0) {
  Write-Host "==> Running my-english-voice ($Preset) [timeout: ${TimeoutSeconds}s]"
  $proc = Start-Process -FilePath $exePath -ArgumentList $arguments -WorkingDirectory $repoRoot -PassThru -NoNewWindow
  $finished = $proc.WaitForExit($TimeoutSeconds * 1000)
  if (-not $finished) {
    Write-Host "==> Process did not exit within ${TimeoutSeconds}s — terminating"
    $proc.Kill()
    $proc.WaitForExit(5000) | Out-Null
    Write-Host "==> Process killed (slow shutdown on CI is not a smoke failure)"
  } elseif ($proc.ExitCode -ne 0) {
    throw "Process exited with code $($proc.ExitCode)"
  }
} else {
  Invoke-MevStep -Description "Running my-english-voice ($Preset)" -Command $exePath -Arguments $arguments -WorkingDirectory $repoRoot
}
