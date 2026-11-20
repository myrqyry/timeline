#!/usr/bin/env python3
from pathlib import Path
import json
import re

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
