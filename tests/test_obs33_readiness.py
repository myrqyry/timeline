#!/usr/bin/env python3
from pathlib import Path
import json
import re
import subprocess

root = Path(__file__).resolve().parents[1]

spec = json.loads((root / "buildspec.json").read_text())
obs33 = spec["dependencies"]["obs-studio"]["targets"]["obs33"]
assert obs33["version"] == "33.0.1"
assert obs33["hashes"]["linux-x86_64"] == "9174ee0e3677c92d0d060bdce4c5d44dfee9fd3afddfb6333f098fd029b8de2b"

workflow = (root / ".github/workflows/linux-build.yml").read_text()
assert "obsCompatibility: obs33" in workflow
assert "ubuntu-26.04" in workflow
# A matrix label alone does not select its OS: the job runner must use matrix.os.
assert re.search(r"(?m)^\s*runs-on:\s*\$\{\{\s*matrix\.os\s*\}\}\s*$", workflow), (
    "Linux CI must select the matrix OS; hardcoded ubuntu-24.04 breaks OBS33 (FFmpeg >=8)"
)

cmake = (root / "CMakeLists.txt").read_text()
assert 'OBS_PLUGIN_DESTINATION' in cmake
assert '_timeline_modern_plugin_destination' in cmake
assert r'bin[/\\]64bit$' in cmake
assert 'DESTINATION "${_timeline_modern_plugin_destination}"' in cmake

tests = (root / "tests/main_test.cpp").read_text()
assert "obs_source_remove(source);" in tests
assert "OBS sources still registered before shutdown" in tests
# A successful compatibility job alone does not produce a usable package.
packaging = (root / ".github/workflows/build-project.yaml").read_text()
ubuntu = packaging.split("  ubuntu-build:", 1)[1].split("  windows-build:", 1)[0]
assert re.search(r"os:\s*ubuntu-26\.04\s*,\s*obsCompatibility:\s*obs33", ubuntu), (
    "OBS33 needs a dedicated Ubuntu 26.04 distributable build"
)
assert "lib/x86_64-linux-gnu/obs-modules/plugins/timeline.so" in ubuntu
assert "share/obs/obs-modules/plugins/timeline/locale/en-US.ini" in ubuntu
assert "ubuntu-26.04-x86_64;tar.xz|deb|ddeb" in (root / ".github/workflows/push.yaml").read_text()
assert "XDG_DATA_HOME" in cmake and ".local/share" in cmake
build_script = (root / ".github/scripts/build-ubuntu").read_text()
assert "-DTIMELINE_SYSTEM_INSTALL=ON" in build_script
assert "-UOBS_PLUGIN_DESTINATION" in build_script and "-UOBS_DATA_DESTINATION" in build_script
assert '"${CMAKE_INSTALL_LIBDIR}/obs-modules/plugins"' in cmake
print("PASS: Timeline OBS33 SDK, runner, package and installation contracts")

# Windows x64 must use the official OBS33 toolchain, not the older SDK.
assert obs33["hashes"].get("windows-x64") == "0f228b960c099c6f6bca9f09a0c3516f14fdf1dd96e1d4702145c5d3ba8cba31"
for dep, digest in [
    ("prebuilt", "7508972a867a0ef68c26cd8bdba61fd999edd0a92fa9eb9585774de9bf51bfbe"),
    ("qt6", "78cd7a138464bb6edcdd3b85fcd63a5a7c69e369629963f3bc395bab74fb3f47"),
]:
    pin = spec["dependencies"][dep]["targets"]["obs33"]
    assert pin["version"] == "2026-10-23"
    assert pin["hashes"]["windows-x64"] == digest
presets = json.loads((root / "CMakePresets.json").read_text())
windows33 = next(p for p in presets["configurePresets"] if p["name"] == "windows-obs33-x64")
assert windows33["generator"] == "Visual Studio 18 2026"
assert windows33["architecture"] == "x64,version=10.0.26100.0"
assert windows33["binaryDir"] != next(p for p in presets["configurePresets"] if p["name"] == "windows-x64")["binaryDir"]
assert windows33["cacheVariables"]["TIMELINE_OBS_COMPATIBILITY"] == "obs33"
assert any(p["name"] == "windows-ci-obs33-x64" for p in presets["buildPresets"])
windows = packaging.split("  windows-build:", 1)[1]
assert re.search(r"os:\s*windows-2025-vs2026\s*,\s*obsCompatibility:\s*obs33", windows)
assert "runs-on: ${{ matrix.os }}" in windows
for name in ("Build-Windows.ps1", "Package-Windows.ps1"):
    script = (root / ".github/scripts" / name).read_text()
    assert "ValidateSet('obs31', 'obs32', 'obs33')" in script
assert "$ENV{ALLUSERSPROFILE}/obs-studio/plugins/timeline" in cmake
print("PASS: OBS33 Windows x64 pinned SDK, runner and package contracts")

# The release-only SDK configuration path must not affect legacy or Debug builds.
subprocess.run(["cmake", "-P", str(root / "tests/test_sdk_configurations.cmake")], check=True)
