/*
    CHAOS REALM — minimal, dependency-free test harness.
    A tiny xUnit-style helper so the DSP layer can be validated with nothing
    but a C++17 compiler (no JUCE, no gtest).
*/
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace chaostest
{

struct Stats { int checks = 0; int failures = 0; };
inline Stats& stats() { static Stats s; return s; }

inline void report (bool ok, const std::string& what, const std::string& extra = {})
{
    ++stats().checks;
    if (!ok)
    {
        ++stats().failures;
        std::printf ("  [FAIL] %s%s%s\n", what.c_str(),
                     extra.empty() ? "" : " — ", extra.c_str());
    }
}

inline void check (bool cond, const std::string& what) { report (cond, what); }

inline void checkClose (float a, float b, float tol, const std::string& what)
{
    const bool ok = std::fabs (a - b) <= tol;
    char buf[128];
    std::snprintf (buf, sizeof (buf), "got %.6g expected %.6g (tol %.3g)", a, b, tol);
    report (ok, what, ok ? "" : buf);
}

inline void checkFinite (const std::vector<float>& v, const std::string& what)
{
    bool ok = true;
    for (float x : v) if (!std::isfinite (x)) { ok = false; break; }
    report (ok, what, ok ? "" : "non-finite sample present");
}

inline void checkBounded (const std::vector<float>& v, float limit, const std::string& what)
{
    float peak = 0.0f;
    for (float x : v) peak = std::max (peak, std::fabs (x));
    char buf[64];
    std::snprintf (buf, sizeof (buf), "peak %.4g > %.4g", peak, limit);
    report (peak <= limit, what, peak <= limit ? "" : buf);
}

inline void section (const std::string& name) { std::printf ("[ %s ]\n", name.c_str()); }

inline int summary()
{
    std::printf ("\n==== %d checks, %d failures ====\n", stats().checks, stats().failures);
    return stats().failures == 0 ? 0 : 1;
}

} // namespace chaostest
