ci_dependencies(){ :; }
ci_build(){
  # Use the upstream vendor header, not tests/include's substitute.
  mkdir "$CI_WORK/headers"
  curl -fsSL --retry 3 https://raw.githubusercontent.com/obsproject/obs-websocket/1ef34bf48110c2a18184e50e41cd0b1a855e2147/lib/obs-websocket-api.h -o "$CI_WORK/headers/obs-websocket-api.h"
  printf '%s  %s\n' c29e8e38ee66c36db79cae86217ab0f270aad39f97e82090809d087c281b7948 "$CI_WORK/headers/obs-websocket-api.h" | sha256sum -c -
  dest=bin/64bit
  [[ "$CI_LAYOUT" != modern ]] || dest=.
  cmake -S "$CI_ROOT" -B "$CI_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DTIMELINE_OBS_COMPATIBILITY="obs$CI_OBS_MAJOR" -DTIMELINE_BUILD_OBS_FROM_SOURCE=OFF \
    -DTIMELINE_BUILD_TESTS=ON -DENABLE_OBS_WEBSOCKET_VENDOR=ON \
    -DOBS_WEBSOCKET_INCLUDE_DIR="$CI_WORK/headers" \
    -Dlibobs_DIR="$CI_SDK/lib/cmake/libobs" -Dobs-frontend-api_DIR="$CI_SDK/lib/cmake/obs-frontend-api" \
    -DOBS_PLUGIN_DESTINATION="$dest" -DOBS_DATA_DESTINATION=data
  cmake --build "$CI_BUILD" --parallel 4
}
ci_test(){
  python3 "$CI_ROOT/tests/test_obs33_readiness.py"
  # CTest wraps the entire Qt suite as one entry; require its actual cases.
  QT_QPA_PLATFORM=xcb TIMELINE_REQUIRE_OBS_RUNTIME=1 xvfb-run -a "$CI_BUILD/tests/run_tests" \
      -o "$CI_WORK/qt.xml,xml" -o -,txt
  python3 - "$CI_WORK/qt.xml" "$MINIMUM_TESTS" <<'XML'
import sys, xml.etree.ElementTree as E
cases=E.parse(sys.argv[1]).findall(".//TestFunction")
assert len(cases)>=int(sys.argv[2]),len(cases)
assert all(c.findall("Incident") and all(i.get("type")=="pass" for i in c.findall("Incident")) for c in cases)
assert not any(c.findall(".//Message[@type='skip']") for c in cases)
print(f"Verified {len(cases)} Qt test functions")
XML
}
ci_stage(){
  # Existing local builds can have absolute user-install destinations;
  # DESTDIR always keeps installation inside a disposable tree.
  mkdir "$CI_WORK/install"
  DESTDIR="$CI_WORK/install" cmake --install "$CI_BUILD" --prefix /package
  mapfile -t binaries < <(find "$CI_WORK/install" -type f -name timeline.so)
  [[ "${#binaries[@]}" -ge 1 && "${#binaries[@]}" -le 2 ]]
  for b in "${binaries[@]}"; do cmp "$b" "${binaries[0]}"; done
  mapfile -t locales < <(find "$CI_WORK/install" -type f -path '*/data/locale/en-US.ini')
  [[ "${#locales[@]}" == 1 ]]
  dest="$CI_STAGE/$MODULE"
  rel=bin/64bit; [[ "$CI_LAYOUT" != modern ]] || rel=.
  mkdir -p "$dest/$rel" "$dest/data/locale"
  cp "${binaries[0]}" "$dest/$rel/$MODULE.so"
  cp "${locales[0]}" "$dest/data/locale/en-US.ini"
  cmp "$CI_ROOT/data/locale/en-US.ini" "$dest/data/locale/en-US.ini"
  cp "$CI_ROOT/LICENSE" "$dest/LICENSE"
}
