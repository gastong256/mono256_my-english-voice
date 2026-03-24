#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_windows_common.sh"

preset="windows-msvc-debug"
config_path="config/pipeline.toml"
declare -a app_args=()

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
    --)
      shift
      app_args+=("$@")
      break
      ;;
    *)
      echo "Unknown argument: $1" >&2
      echo "Usage: $0 [--preset windows-msvc-debug] [--config config/pipeline.toml] [-- app args...]" >&2
      exit 1
      ;;
  esac
done

repo_root="$(mev_repo_root)"
mev_require_windows_visible_repo "${repo_root}"
mev_require_command wslpath
ps_cmd="$(mev_powershell_command)"
repo_root_win="$(mev_repo_root_win)"
script_win="$(mev_script_root_win "${repo_root}/scripts/windows/run.ps1")"

if [[ "${config_path}" = /* ]]; then
  config_path="$(wslpath -w "${config_path}")"
fi

args=(-NoProfile -ExecutionPolicy Bypass -File "${script_win}" -RepoRoot "${repo_root_win}" -Preset "${preset}" -ConfigPath "${config_path}")
if [[ ${#app_args[@]} -gt 0 ]]; then
  args+=(-AppArgs)
  args+=("${app_args[@]}")
fi

"${ps_cmd}" "${args[@]}"
