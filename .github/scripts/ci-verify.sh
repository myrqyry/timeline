#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
pin=e9eb704437e606721ee92d7d064c4b8168fd8f53
engine="$RUNNER_TEMP/obs-ci-engine"
git init -q "$engine"
git -C "$engine" fetch --depth=1 https://github.com/myrqyry/obs-plugin-ci.git "$pin"
git -C "$engine" checkout --detach FETCH_HEAD
[[ "$(git -C "$engine" rev-parse HEAD)" == "$pin" ]]
rustup toolchain install 1.93.1 --profile minimal
cargo +1.93.1 build --release --locked --manifest-path "$engine/Cargo.toml"
export OBS_CI_SCRIPTS="$engine/scripts"
python "$root/.github/scripts/test-native-caller.py"
if [[ "$TARGET" == linux-x86_64 ]]; then
  exec bash "$engine/scripts/run-linux-caller.sh" "$root"
fi
exec python "$engine/scripts/native_caller.py" "$root"
