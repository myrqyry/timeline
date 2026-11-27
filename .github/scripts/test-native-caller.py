import importlib.util
import os
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,os.environ["OBS_CI_SCRIPTS"])
spec=importlib.util.spec_from_file_location("hook",Path(__file__).with_name("ci-native.py"))
hook=importlib.util.module_from_spec(spec)
spec.loader.exec_module(hook)
class Test(unittest.TestCase):
    def test_package_layouts(self):
        for target in ("windows-x64","macos-arm64","macos-x86_64"):
            for version in ("32.2.0","33.0.3"):
                with self.subTest(target=target,version=version), tempfile.TemporaryDirectory() as tmp:
                    work=Path(tmp)
                    installed=work/"installed"
                    if target.startswith("macos"):
                        binary=installed/"timeline.plugin/Contents/MacOS/timeline"
                        data=binary.parent.parent/"Resources"
                        binary.parent.mkdir(parents=True)
                        (binary.parent.parent/"Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable":"timeline","CFBundlePackageType":"BNDL"}))
                    else:
                        binary=installed/("bin/64bit" if version=="32.2.0" else "")/"timeline.dll"
                        data=installed/"data"
                        binary.parent.mkdir(parents=True)
                    binary.write_bytes(b"module")
                    (data/"locale").mkdir(parents=True)
                    (data/"locale/en-US.ini").write_bytes((ROOT/"data/locale/en-US.ini").read_bytes())
                    out=hook.stage(ROOT,installed,work/"package",target,version)
                    self.assertEqual(out.read_bytes(),b"module")
                    self.assertTrue(out.is_file())
if __name__=="__main__":unittest.main()
