[CmdletBinding()]
param(
  [string]$RepoRoot,
  [string]$Preset = "windows-msvc-debug",
  [string]$ConfigPath = "config/pipeline.windows.toml",
  [string]$VcpkgRoot,
  [string]$OnnxRuntimeRoot,
  [switch]$BuildFirst,
  [string[]]$AppArgs = @()
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
    throw "Build failed before running self-test."
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

$arguments = @("--self-test", "--config", $resolvedConfigPath)
if ($AppArgs.Count -gt 0) {
  $arguments += $AppArgs
}

Invoke-MevStep -Description "Running self-test ($Preset)" -Command $exePath -Arguments $arguments -WorkingDirectory $repoRoot
