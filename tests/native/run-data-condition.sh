#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
module="$(realpath "$1")"
provider="$(realpath "$2")"
providerData="$(realpath "$3")"
root=$(mktemp -d /tmp/timeline-data-native.XXXXXX)
echo "Evidence: $root"
g++ -std=c++17 -fPIC -shared "$repo/tests/native/data-condition-probe.cpp" -o "$root/probe.so" $(pkg-config --cflags --libs Qt6Widgets) -I/usr/include/obs -lobs -lobs-frontend-api
python3 - "$root" <<'PY' &
import http.server,pathlib,sys,json
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=json.dumps({"value":True if self.path=="/bool" else .75}).encode()
        self.send_response(200);self.send_header("Content-Type","application/json");self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
pathlib.Path(sys.argv[1],"port").write_text(str(server.server_port))
server.serve_forever()
PY
server=$!
trap 'kill "$server" 2>/dev/null || true' EXIT
for i in $(seq 1 30); do [[ -s "$root/port" ]] && break; sleep .1; done
for mode in yes no; do
 home="$root/$mode";cfg="$home/.config/obs-studio"
 mkdir -p "$cfg/plugins/timeline/bin/64bit" "$cfg/plugins/timeline/data" "$cfg/plugins/data-condition-probe/bin/64bit" "$cfg/plugin_config/timeline"
 cp "$module" "$cfg/plugins/timeline/bin/64bit/timeline.so"
 cp -a "$repo/data/." "$cfg/plugins/timeline/data/"
 cp "$root/probe.so" "$cfg/plugins/data-condition-probe/bin/64bit/data-condition-probe.so"
 if [[ "$mode" == yes ]];then
  mkdir -p "$cfg/plugins/obs-urlsource/bin/64bit" "$cfg/plugins/obs-urlsource/data"
  cp "$provider" "$cfg/plugins/obs-urlsource/bin/64bit/obs-urlsource.so"
  cp -a "$providerData/." "$cfg/plugins/obs-urlsource/data/"
 fi
 printf '[General]\nFirstRun=true\n' >"$cfg/user.ini"
 python3 - "$cfg/plugin_config/timeline/timeline.json" <<'PY'
import json,sys
events=[]
def cue(t,target,op,threshold,age):
    events.append(dict(time=t,type=16,source=target,data=json.dumps(dict(uuid="aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",op=op,threshold=threshold,maxAgeMs=age))))
cue(500,"Cue second",">=",0.5,5000)
cue(1500,"Cue third","<",0.5,5000)
cue(2600,"Cue third",">=",0.5,5000)
cue(3700,"Cue third",">=",0.5,100)
cue(4800,"Cue third","true",0,5000)
cue(5900,"Cue first","true",0,5000)
events.append(dict(time=7000,type=0,source="Cue second"))
with open(sys.argv[1],"w") as f:json.dump(dict(events=events,settings=dict(endTime=7.2)),f)
PY
 set +e
 timeout 24s xvfb-run -a -s '-screen 0 1600x1000x24' env HOME="$home" XDG_CONFIG_HOME="$home/.config" XDG_CACHE_HOME="$home/.cache" XDG_DATA_HOME="$home/.local/share" DATACUE_ROOT="$root" DATACUE_PROVIDER="$mode" LIBGL_ALWAYS_SOFTWARE=1 NO_PROXY=127.0.0.1 obs --multi --disable-shutdown-check --no-updater >"$root/$mode.log" 2>&1
 rc=$?
 set -e
 echo "Provider $mode: OBS_EXIT=$rc"
 grep DATACUE "$root/$mode.log" || true
 [[ "$rc" == 0 ]]
 [[ $(grep -c 'DATACUE .*=[1]$' "$root/$mode.log") == 9 ]]
 ! grep -q 'DATACUE .*=[0]$' "$root/$mode.log"
done
