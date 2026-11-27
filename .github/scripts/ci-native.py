from pathlib import Path
import shutil
from native_caller import layout, verify_payload
from native_sdk import download

MODULE = "timeline"

def configure(root, work, target, version):
    header = work / "headers"
    header.mkdir()
    download("https://raw.githubusercontent.com/obsproject/obs-websocket/1ef34bf48110c2a18184e50e41cd0b1a855e2147/lib/obs-websocket-api.h",
             header / "obs-websocket-api.h",
             "c29e8e38ee66c36db79cae86217ab0f270aad39f97e82090809d087c281b7948")
    args = ["-DTIMELINE_EXTERNAL_SDK=ON", "-DTIMELINE_BUILD_OBS_FROM_SOURCE=OFF",
            "-DTIMELINE_BUILD_TESTS=OFF", "-DENABLE_OBS_WEBSOCKET_VENDOR=ON",
            "-DOBS_WEBSOCKET_INCLUDE_DIR=" + header.as_posix(),
            "-DTIMELINE_OBS_COMPATIBILITY=obs" + version.split(".")[0]]
    if target == "windows-x64":
        args += ["-DOBS_PLUGIN_DESTINATION=" + ("bin/64bit" if version=="32.2.0" else "."),
                 "-DOBS_DATA_DESTINATION=data"]
    return args

def stage(root, installed, payload, target, version):
    binary, data, notice = layout(payload, MODULE, target, version)
    if target.startswith("macos-"):
        shutil.copytree(installed / "timeline.plugin", payload / "timeline.plugin")
    else:
        original = installed / ("bin/64bit" if version=="32.2.0" else "") / "timeline.dll"
        other = installed / "timeline.dll"
        if other != original and other.exists() and other.read_bytes() != original.read_bytes():
            raise ValueError("Legacy and modern binaries differ")
        binary.parent.mkdir(parents=True)
        shutil.copy2(original, binary)
        shutil.copytree(installed / "data", data)
    license_bytes = (root / "LICENSE").read_bytes()
    notice.write_bytes(license_bytes)
    return verify_payload(payload, MODULE, target, version,
        {"locale/en-US.ini": (root / "data/locale/en-US.ini").read_bytes()}, license_bytes)
