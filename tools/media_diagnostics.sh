#!/bin/sh
set -u

printf '%s\n' '== Vantage media diagnostics =='
printf 'session: %s\n' "${XDG_SESSION_TYPE:-unknown}"
printf 'pipewire remote: %s\n' "${PIPEWIRE_REMOTE:-default}"

printf '\n%s\n' '-- WebKitGTK --'
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists webkitgtk-6.0; then
  printf 'webkitgtk-6.0: %s\n' "$(pkg-config --modversion webkitgtk-6.0)"
else
  printf '%s\n' 'webkitgtk-6.0: not visible to pkg-config'
fi

printf '\n%s\n' '-- GStreamer plugins --'
if command -v gst-inspect-1.0 >/dev/null 2>&1; then
  for plugin in pipewire libcamerasrc v4l2src; do
    if gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
      printf '%-14s %s\n' "$plugin" 'available'
    else
      printf '%-14s %s\n' "$plugin" 'MISSING'
    fi
  done
else
  printf '%s\n' 'gst-inspect-1.0 missing (Ubuntu: sudo apt install gstreamer1.0-tools)'
fi

printf '\n%s\n' '-- Video devices visible to GStreamer --'
if command -v gst-device-monitor-1.0 >/dev/null 2>&1; then
  # Device monitor may wait indefinitely on broken backends. Bound it when the
  # coreutils timeout command is available while retaining a portable fallback.
  if command -v timeout >/dev/null 2>&1; then
    timeout 5s gst-device-monitor-1.0 Video/Source 2>&1 || true
  else
    gst-device-monitor-1.0 Video/Source 2>&1 || true
  fi
else
  printf '%s\n' 'gst-device-monitor-1.0 missing (Ubuntu: sudo apt install gstreamer1.0-tools)'
fi

printf '\n%s\n' '-- PipeWire services --'
if command -v systemctl >/dev/null 2>&1; then
  for unit in pipewire.service wireplumber.service; do
    state=$(systemctl --user is-active "$unit" 2>/dev/null || true)
    printf '%-22s %s\n' "$unit" "${state:-unknown}"
  done
fi

printf '\n%s\n' '-- Native video nodes --'
if ls /dev/video* >/dev/null 2>&1; then
  ls -l /dev/video*
else
  printf '%s\n' 'no /dev/video* nodes found'
fi
