#pragma once
// Tiny zero-dependency test framework: TEST() registers a case, CHECK/CHECK_EQ
// record failures. test_main.cpp runs every registered case and reports.
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> r;
    return r;
}

struct TestRegistrar {
    TestRegistrar(const std::string& n, std::function<void()> f) {
        testRegistry().push_back({n, std::move(f)});
    }
};

extern int g_testFailures;

#define TEST(name)                                                      \
    static void name();                                                 \
    static TestRegistrar registrar_##name(#name, name);                 \
    static void name()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "    FAIL: " << #cond << "  @ " << __FILE__ << ":" << __LINE__ \
                      << "\n";                                                          \
            ++g_testFailures;                                                          \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                               \
        auto _va = (a);                                                                \
        auto _vb = (b);                                                                \
        if (!(_va == _vb)) {                                                           \
            std::cerr << "    FAIL: " << #a << " == " << #b << "  (" << _va << " vs "  \
                      << _vb << ")  @ " << __FILE__ << ":" << __LINE__ << "\n";        \
            ++g_testFailures;                                                          \
        }                                                                              \
    } while (0)
