[CmdletBinding()]
param(
  [string]$RepoRoot,
  [string]$Preset = "windows-msvc-full",
  [string[]]$ConfigPaths = @("config/pipeline.windows.preview.toml", "config/pipeline.windows.toml"),
  [string]$VcpkgRoot,
  [string]$OnnxRuntimeRoot,
  [string]$ArtifactRoot,
  [string[]]$AppArgs = @(),
  [int]$RunDurationSeconds = 12,
  [switch]$BuildFirst,
  [switch]$SkipSynthetic,
  [switch]$SkipSelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

function Get-MevPercentile {
  param(
    [double[]]$Values,
    [double]$Quantile
  )

  if (-not $Values -or $Values.Count -eq 0) {
    return 0.0
  }

  $sorted = $Values | Sort-Object
  $index = [int][Math]::Floor(([Math]::Max([Math]::Min($Quantile, 1.0), 0.0)) * ($sorted.Count - 1))
  return [double]$sorted[$index]
}

function Parse-MevRunMetrics {
  param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath
  )

  $totals = New-Object System.Collections.Generic.List[double]
  $asr = New-Object System.Collections.Generic.List[double]
  $tts = New-Object System.Collections.Generic.List[double]
  $chunks = New-Object System.Collections.Generic.List[double]
  $previewChunks = 0
  $queueDrops = 0
  $underruns = 0
  $staleCancelled = 0

  foreach ($line in Get-Content -Path $LogPath) {
    if ($line -match 'total_ms=([0-9.]+)\s+asr_ms=([0-9.]+)\s+tts_ms=([0-9.]+)\s+speech_chunks=([0-9]+)\s+preview=([01])') {
      $totals.Add([double]$matches[1]) | Out-Null
      $asr.Add([double]$matches[2]) | Out-Null
      $tts.Add([double]$matches[3]) | Out-Null
      $chunks.Add([double]$matches[4]) | Out-Null
      if ([int]$matches[5] -eq 1) {
        $previewChunks += 1
      }
      continue
    }

    if ($line -match 'shutdown metrics queue_drops=([0-9]+)\s+output_underruns=([0-9]+)\s+stale_cancelled=([0-9]+)') {
      $queueDrops = [int]$matches[1]
      $underruns = [int]$matches[2]
      $staleCancelled = [int]$matches[3]
    }
  }

  return @{
    utterance_count = $totals.Count
    ttfa_audio_p50_ms = (Get-MevPercentile -Values $totals.ToArray() -Quantile 0.50)
    ttfa_audio_p95_ms = (Get-MevPercentile -Values $totals.ToArray() -Quantile 0.95)
    asr_p50_ms = (Get-MevPercentile -Values $asr.ToArray() -Quantile 0.50)
    asr_p95_ms = (Get-MevPercentile -Values $asr.ToArray() -Quantile 0.95)
    tts_p50_ms = (Get-MevPercentile -Values $tts.ToArray() -Quantile 0.50)
    tts_p95_ms = (Get-MevPercentile -Values $tts.ToArray() -Quantile 0.95)
    clause_latency_p50_ms = (Get-MevPercentile -Values $totals.ToArray() -Quantile 0.50)
    clause_latency_p95_ms = (Get-MevPercentile -Values $totals.ToArray() -Quantile 0.95)
    jitter_ms = ((Get-MevPercentile -Values $totals.ToArray() -Quantile 0.95) - (Get-MevPercentile -Values $totals.ToArray() -Quantile 0.50))
    avg_speech_chunks = (if ($chunks.Count -gt 0) { ($chunks | Measure-Object -Average).Average } else { 0.0 })
    preview_chunk_runs = $previewChunks
    output_underruns = $underruns
    queue_drops = $queueDrops
    stale_cancelled = $staleCancelled
    gpu_busy_time_ms = $null
  }
}

function Get-MevBenchmarkLabel {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath
  )

  $name = [System.IO.Path]::GetFileNameWithoutExtension($ConfigPath)
  if ($name -like "*preview*") { return "interactive_preview" }
  if ($name -like "*smoke*") { return "interactive_preview_smoke" }
  if ($name -like "*cuda*") { return "interactive_balanced_cuda" }
  return "interactive_balanced"
}

$repoRoot = Get-MevRepoRoot -RepoRoot $RepoRoot
Assert-MevWindowsCheckout -RepoRoot $repoRoot
Assert-MevCommand -Name "cmake"

[void](Set-MevBuildEnvironment -RepoRoot $repoRoot -VcpkgRoot $VcpkgRoot -OnnxRuntimeRoot $OnnxRuntimeRoot)

if ($BuildFirst) {
  & (Join-Path $PSScriptRoot "build.ps1") -RepoRoot $repoRoot -Preset $Preset -VcpkgRoot $env:VCPKG_ROOT -OnnxRuntimeRoot $env:ONNXRUNTIME_ROOT
  if ($LASTEXITCODE -ne 0) {
    throw "Benchmark build failed."
  }
}

$exePath = Get-MevExecutablePath -RepoRoot $repoRoot -Preset $Preset
if (-not (Test-Path -Path $exePath)) {
  throw "Executable not found: $exePath. Run build.ps1 first or use -BuildFirst."
}

$benchmarkExe = Get-MevBenchmarkExecutablePath -RepoRoot $repoRoot -Preset $Preset
if (-not (Test-Path -Path $benchmarkExe)) {
  throw "Benchmark executable not found: $benchmarkExe. Ensure benchmarks are built."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ($ArtifactRoot) {
  $artifactRoot = $ArtifactRoot
} else {
  $artifactRoot = Join-Path $repoRoot "artifacts\benchmarks\$timestamp"
}
New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null

$summary = [ordered]@{
  generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
  preset = $Preset
  repo_root = $repoRoot
  run_duration_seconds = $RunDurationSeconds
  app_args = @($AppArgs)
  synthetic = @{}
  sessions = @()
}

if (-not $SkipSynthetic) {
  foreach ($mode in @("interactive_preview", "interactive_balanced")) {
    $syntheticOut = Join-Path $artifactRoot "synthetic-$mode.json"
    Invoke-MevStep -Description "Running synthetic benchmark ($mode)" -Command $benchmarkExe -Arguments @(
      "--mode", $mode,
      "--summary-out", $syntheticOut
    ) -WorkingDirectory $repoRoot
    $summary.synthetic[$mode] = Get-Content -Path $syntheticOut -Raw | ConvertFrom-Json
  }
}

foreach ($configPath in $ConfigPaths) {
  $resolvedConfigPath = Resolve-MevConfigPath -RepoRoot $repoRoot -ConfigPath $ConfigPath
  if (-not (Test-Path -Path $resolvedConfigPath)) {
    throw "Config file not found: $resolvedConfigPath"
  }

  $label = Get-MevBenchmarkLabel -ConfigPath $resolvedConfigPath
  $sessionDir = Join-Path $artifactRoot $label
  New-Item -ItemType Directory -Force -Path $sessionDir | Out-Null

  if (-not $SkipSelfTest) {
    $selfTestLog = Join-Path $sessionDir "self-test.log"
    Write-Host "==> Running self-test ($label)"
    & $exePath @("--self-test", "--config", $resolvedConfigPath) @AppArgs *>&1 | Tee-Object -FilePath $selfTestLog
    if ($LASTEXITCODE -ne 0) {
      throw "Self-test failed for $label. See $selfTestLog"
    }
  }

  $runLog = Join-Path $sessionDir "run.log"
  Write-Host "==> Running latency session ($label)"
  & $exePath @("--config", $resolvedConfigPath, "--runtime.run_duration_seconds", "$RunDurationSeconds") @AppArgs *>&1 | Tee-Object -FilePath $runLog
  if ($LASTEXITCODE -ne 0) {
    throw "Run failed for $label. See $runLog"
  }

  $metrics = Parse-MevRunMetrics -LogPath $runLog
  $sessionSummary = [ordered]@{
    label = $label
    config_path = $resolvedConfigPath
    metrics = $metrics
    notes = @(
      "TTFA metrics come from [METRICS] total_ms in application logs.",
      "gpu_busy_time_ms is null until a dedicated GPU occupancy probe exists."
    )
  }

  $sessionJsonPath = Join-Path $sessionDir "summary.json"
  $sessionSummary | ConvertTo-Json -Depth 6 | Set-Content -Path $sessionJsonPath -Encoding ASCII
  $summary.sessions += $sessionSummary
}

$summaryPath = Join-Path $artifactRoot "summary.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryPath -Encoding ASCII

$markdown = @()
$markdown += "# Windows Latency Benchmark"
$markdown += ""
$markdown += "- Preset: ``$Preset``"
$markdown += "- Generated: ``$($summary.generated_at_utc)``"
$markdown += "- Run duration: ``$RunDurationSeconds`` seconds"
$markdown += ""
$markdown += "## Sessions"
foreach ($session in $summary.sessions) {
  $markdown += ""
  $markdown += "### $($session.label)"
  $markdown += ""
  $markdown += "- Config: ``$($session.config_path)``"
  $markdown += "- Utterances: ``$($session.metrics.utterance_count)``"
  $markdown += "- TTFA p50: ``$($session.metrics.ttfa_audio_p50_ms) ms``"
  $markdown += "- TTFA p95: ``$($session.metrics.ttfa_audio_p95_ms) ms``"
  $markdown += "- ASR p50: ``$($session.metrics.asr_p50_ms) ms``"
  $markdown += "- TTS p50: ``$($session.metrics.tts_p50_ms) ms``"
  $markdown += "- Jitter: ``$($session.metrics.jitter_ms) ms``"
  $markdown += "- Output underruns: ``$($session.metrics.output_underruns)``"
  $markdown += "- Queue drops: ``$($session.metrics.queue_drops)``"
}

$markdownPath = Join-Path $artifactRoot "summary.md"
$markdown -join "`r`n" | Set-Content -Path $markdownPath -Encoding ASCII

Write-Host "==> Benchmark artifacts written to $artifactRoot"
Write-Host "==> Summary JSON: $summaryPath"
Write-Host "==> Summary Markdown: $markdownPath"
