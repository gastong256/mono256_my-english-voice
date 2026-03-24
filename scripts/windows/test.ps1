[CmdletBinding()]
param(
  [string]$RepoRoot,
  [string]$Preset = "windows-msvc-debug",
  [string]$VcpkgRoot,
  [string]$OnnxRuntimeRoot,
  [switch]$BuildFirst,
  [string[]]$CTestArgs = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-MevRepoRoot -RepoRoot $RepoRoot
Assert-MevWindowsCheckout -RepoRoot $repoRoot
Assert-MevCommand -Name "ctest"

[void](Set-MevBuildEnvironment -RepoRoot $repoRoot -VcpkgRoot $VcpkgRoot -OnnxRuntimeRoot $OnnxRuntimeRoot)

if ($BuildFirst) {
  & (Join-Path $PSScriptRoot "build.ps1") -RepoRoot $repoRoot -Preset $Preset -VcpkgRoot $env:VCPKG_ROOT -OnnxRuntimeRoot $env:ONNXRUNTIME_ROOT
  if ($LASTEXITCODE -ne 0) {
    throw "Build failed before running tests."
  }
}

$arguments = @("--preset", $Preset, "--output-on-failure")
if ($CTestArgs.Count -gt 0) {
  $arguments += $CTestArgs
}

Invoke-MevStep -Description "Running tests for preset $Preset" -Command "ctest" -Arguments $arguments -WorkingDirectory $repoRoot
