#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
module="$(realpath "$1")"
root=$(mktemp -d /tmp/timeline-macro-native.XXXXXX)
echo "Evidence: $root"
home="$root/home"
cfg="$home/.config/obs-studio"
mkdir -p "$cfg/plugins/timeline/bin/64bit" "$cfg/plugins/timeline/data" "$cfg/plugins/macro-layout-probe/bin/64bit" "$cfg/plugin_config/timeline"
cp "$module" "$cfg/plugins/timeline/bin/64bit/timeline.so"
cp -a "$repo/data/." "$cfg/plugins/timeline/data/"
g++ -std=c++17 -fPIC -shared "$repo/tests/native/macro-layout-probe.cpp" -o "$cfg/plugins/macro-layout-probe/bin/64bit/macro-layout-probe.so" $(pkg-config --cflags --libs Qt6Widgets Qt6Test) -I/usr/include/obs -lobs -lobs-frontend-api
printf '[General]\nFirstRun=true\n' >"$cfg/user.ini"
python3 - "$cfg/plugin_config/timeline/macro_buttons.json" <<'PY'
import json,sys
def action(id,label,scene):
    return dict(id=id,label=label,type=0,source=scene,color="#55aaff")
grid=dict(id="grid",label="Scenes",type=-1,control="button-grid",slots=[action("switch","Second","Macro probe second"),action("back","First","Macro probe first"),None,None])
fader=dict(id="fader",label="Volume",type=-1,control="slider",binding=dict(sourceUuid="aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",filterUuid="",key="timeline:mixer:volume",type="float",domain="mixer"))
root=dict(version=2,activeStrip="main",strips=[dict(id="main",name="Macro Strip",orientation="horizontal",buttons=[])],individualStrips=[
    dict(id="audio",name="Audio",orientation="vertical",buttons=[fader]),
    dict(id="scenes",name="Scenes",orientation="horizontal",buttons=[grid,action("other","Back","Macro probe first")])])
with open(sys.argv[1],"w") as f:json.dump(root,f)
PY
for restart in 0 1; do
  set +e
  timeout 35s xvfb-run -a -s '-screen 0 1600x1000x24' env HOME="$home" XDG_CONFIG_HOME="$home/.config" XDG_CACHE_HOME="$home/.cache" XDG_DATA_HOME="$home/.local/share" MACROLAYOUT_ROOT="$root" MACROLAYOUT_RESTART="$restart" LIBGL_ALWAYS_SOFTWARE=1 obs --multi --disable-shutdown-check --no-updater --disable-missing-files-check >"$root/run-$restart.log" 2>&1
  rc=$?
  set -e
  echo "Restart=$restart OBS_EXIT=$rc"
  grep MACROLAYOUT "$root/run-$restart.log" || true
  [[ "$rc" == 0 ]]
  ! grep -Eq 'MACROLAYOUT .*=(0)$' <(grep -v 'MACROLAYOUT failures=' "$root/run-$restart.log")
  if [[ "$restart" == 0 ]]; then
    [[ $(grep -c 'MACROLAYOUT .*=[1]$' "$root/run-$restart.log") == 15 ]]
  else
    [[ $(grep -c 'MACROLAYOUT .*=[1]$' "$root/run-$restart.log") == 5 ]]
  fi
done
