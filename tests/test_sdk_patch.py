"""Exercise the actual CMake SDK patch against isolated source files."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
module = root / "cmake/common/buildspec_common.cmake"
original = """bool obs_property_list_item_bool(obs_property_t *p, size_t idx)
{
\tstruct list_data *data = get_list_fmt_data(p, OBS_COMBO_FORMAT_BOOL);
\treturn (data && idx < data->items.num) ? data->items.array[idx].d : false;
}
"""
corrected = original.replace("array[idx].d : false", "array[idx].b : false")
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "obs-properties.c"
    runner = Path(directory) / "run.cmake"
    runner.write_text(f'include("{module.as_posix()}")\n_timeline_patch_obs33_bool("{source.as_posix()}")\n')
    def run():
        return subprocess.run(["cmake", "-P", str(runner)], capture_output=True, text=True)
    for content, expected in [(original, corrected), (corrected, corrected)]:
        source.write_text(content)
        result = run()
        assert result.returncode == 0, result.stderr
        assert source.read_text() == expected
    for content in [original + original, original + corrected, original.replace("COMBO_FORMAT_BOOL", "COMBO_FORMAT_FLOAT"), ""]:
        source.write_text(content)
        result = run()
        assert result.returncode != 0, result.stdout
        assert source.read_text() == content
print("PASS: exact SDK boolean getter patch, idempotence and rejection")
