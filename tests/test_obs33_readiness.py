#!/usr/bin/env python3
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]

spec = json.loads((root / "buildspec.json").read_text())
obs33 = spec["dependencies"]["obs-studio"]["targets"]["obs33"]
assert obs33["version"] == "33.0.1"
assert obs33["hashes"]["linux-x86_64"] == "9174ee0e3677c92d0d060bdce4c5d44dfee9fd3afddfb6333f098fd029b8de2b"

workflow = (root / ".github/workflows/linux-build.yml").read_text()
assert "obsCompatibility: obs33" in workflow
assert "ubuntu-26.04" in workflow

cmake = (root / "CMakeLists.txt").read_text()
assert 'OBS_PLUGIN_DESTINATION' in cmake
assert '_timeline_modern_plugin_destination' in cmake
assert r'bin[/\\]64bit$' in cmake
assert 'DESTINATION "${_timeline_modern_plugin_destination}"' in cmake

tests = (root / "tests/main_test.cpp").read_text()
assert "obs_source_remove(source);" in tests
assert "OBS sources still registered before shutdown" in tests
print("PASS: Timeline has stable OBS 33 coverage, deterministic teardown, and modern Windows layout")
