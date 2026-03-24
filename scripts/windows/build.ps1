[CmdletBinding()]
param(
  [string]$RepoRoot,
  [string]$Preset = "windows-msvc-debug",
  [string]$VcpkgRoot,
  [string]$OnnxRuntimeRoot,
  [switch]$ConfigureOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-MevRepoRoot -RepoRoot $RepoRoot
Assert-MevWindowsCheckout -RepoRoot $repoRoot
Assert-MevCommand -Name "cmake"

[void](Set-MevBuildEnvironment -RepoRoot $repoRoot -VcpkgRoot $VcpkgRoot -OnnxRuntimeRoot $OnnxRuntimeRoot)

Invoke-MevStep -Description "Configuring preset $Preset" -Command "cmake" -Arguments @(
  "--preset",
  $Preset
) -WorkingDirectory $repoRoot

if (-not $ConfigureOnly) {
  Invoke-MevStep -Description "Building preset $Preset" -Command "cmake" -Arguments @(
    "--build",
    "--preset",
    $Preset
  ) -WorkingDirectory $repoRoot
}
