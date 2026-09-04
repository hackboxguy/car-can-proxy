#pragma once
#include <cstdio>
static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define REPORT(name) (std::printf(name ": %d passed, %d failed\n", g_pass, g_fail), g_fail ? 1 : 0)
