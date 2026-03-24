#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_windows_common.sh"

preset="windows-msvc-debug"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      echo "Usage: $0 [--preset windows-msvc-debug]" >&2
      exit 1
      ;;
  esac
done

repo_root="$(mev_repo_root)"
mev_require_windows_visible_repo "${repo_root}"
mev_require_command wslpath
ps_cmd="$(mev_powershell_command)"
repo_root_win="$(mev_repo_root_win)"
script_win="$(mev_script_root_win "${repo_root}/scripts/windows/build.ps1")"

"${ps_cmd}" -NoProfile -ExecutionPolicy Bypass -File "${script_win}" -RepoRoot "${repo_root_win}" -Preset "${preset}"
