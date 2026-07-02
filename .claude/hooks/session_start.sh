#!/usr/bin/env bash
# CHAOS REALM — SessionStart hook.
# Verifies the toolchain and smoke-builds the framework-free DSP core so a web
# session can immediately run and extend the audio tests.  Never fails the
# session (always exits 0); it only reports status.
set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root" || exit 0

echo "── CHAOS REALM session setup ──"

if command -v cmake >/dev/null 2>&1; then
  echo "cmake: $(cmake --version | head -1)"
else
  echo "cmake: NOT FOUND (DSP tests can still build directly with g++)"
fi

if command -v g++ >/dev/null 2>&1; then
  echo "g++:   $(g++ --version | head -1)"
  if g++ -std=c++17 -O1 -fsyntax-only tests/dsp_core_tests.cpp 2>/dev/null; then
    echo "DSP core headers: syntax OK"
  else
    echo "DSP core headers: syntax check reported issues (see above)"
  fi
else
  echo "g++:   NOT FOUND"
fi

echo "Run the DSP suites with:"
echo "  g++ -std=c++17 -O2 tests/dsp_core_tests.cpp -o /tmp/core && /tmp/core"
echo "  g++ -std=c++17 -O2 tests/module_stability_tests.cpp Source/dsp/ChaosEngine.cpp Source/dsp/modules/*.cpp -ISource -o /tmp/stab && /tmp/stab"
echo "───────────────────────────────"
exit 0
