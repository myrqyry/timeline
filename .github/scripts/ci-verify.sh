#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
pin=238f06a01f1469a21fe6991df7f94ef149472be1
engine="$RUNNER_TEMP/obs-ci-engine"
git init -q "$engine"
git -C "$engine" fetch --depth=1 https://github.com/myrqyry/obs-plugin-ci.git "$pin"
git -C "$engine" checkout --detach FETCH_HEAD
[[ "$(git -C "$engine" rev-parse HEAD)" == "$pin" ]]
rustup toolchain install 1.93.1 --profile minimal
cargo +1.93.1 build --release --locked --manifest-path "$engine/Cargo.toml"
exec bash "$engine/scripts/run-linux-caller.sh" "$root"
