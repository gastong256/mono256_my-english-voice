#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_windows_common.sh"

preset="windows-msvc-smoke"
config_path="config/pipeline.windows.smoke.toml"
run_duration_seconds="3"
declare -a smoke_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    --config)
      config_path="$2"
      shift 2
      ;;
    --run-duration-seconds)
      run_duration_seconds="$2"
      shift 2
      ;;
    --skip-build|--skip-tests|--skip-self-test)
      smoke_args+=("$1")
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      echo "Usage: $0 [--preset windows-msvc-smoke] [--config config/pipeline.windows.smoke.toml] [--run-duration-seconds 3] [--skip-build] [--skip-tests] [--skip-self-test]" >&2
      exit 1
      ;;
  esac
done

repo_root="$(mev_repo_root)"
mev_require_windows_visible_repo "${repo_root}"
mev_require_command wslpath
ps_cmd="$(mev_powershell_command)"
repo_root_win="$(mev_repo_root_win)"
script_win="$(mev_script_root_win "${repo_root}/scripts/windows/smoke.ps1")"

if [[ "${config_path}" = /* ]]; then
  config_path="$(wslpath -w "${config_path}")"
fi

args=(-NoProfile -ExecutionPolicy Bypass -File "${script_win}" -RepoRoot "${repo_root_win}" -Preset "${preset}" -ConfigPath "${config_path}" -RunDurationSeconds "${run_duration_seconds}")
if [[ ${#smoke_args[@]} -gt 0 ]]; then
  args+=("${smoke_args[@]}")
fi

"${ps_cmd}" "${args[@]}"
