#pragma once

// A ~40-line stand-in for GoogleTest.
//
// Trade-off (documented, per spec section 5's testing recommendation):
// this environment has no network access to fetch/build GoogleTest, so
// TinySearch ships a tiny macro-based test runner instead. It supports
// exactly what these tests need — named cases, EXPECT/ASSERT with a
// message on failure, and a summary — and nothing more. If GoogleTest is
// available in your own build environment, porting these tests is
// mechanical (TEST_CASE -> TEST, EXPECT_EQ stays EXPECT_EQ, etc).
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace minitest {

struct Registry {
    static std::vector<std::pair<std::string, void (*)()>>& cases() {
        static std::vector<std::pair<std::string, void (*)()>> c;
        return c;
    }
    static int& failures() {
        static int f = 0;
        return f;
    }
    static int& assertions() {
        static int a = 0;
        return a;
    }
};

struct Registrar {
    Registrar(const std::string& name, void (*fn)()) { Registry::cases().emplace_back(name, fn); }
};

inline int run_all() {
    int failed_cases = 0;
    for (auto& [name, fn] : Registry::cases()) {
        int before = Registry::failures();
        try {
            fn();
        } catch (const std::exception& e) {
            std::cerr << "[EXCEPTION] " << name << ": " << e.what() << "\n";
            ++Registry::failures();
        }
        if (Registry::failures() > before) {
            std::cerr << "[FAIL] " << name << "\n";
            ++failed_cases;
        } else {
            std::cout << "[ OK ] " << name << "\n";
        }
    }
    std::cout << "\n" << Registry::assertions() << " assertions, " << failed_cases
               << " failing test case(s) out of " << Registry::cases().size() << "\n";
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace minitest

#define TEST_CASE(name)                                                          \
    void test_##name();                                                          \
    static minitest::Registrar registrar_##name(#name, &test_##name);            \
    void test_##name()

#define EXPECT_TRUE(cond)                                                        \
    do {                                                                         \
        ++minitest::Registry::assertions();                                      \
        if (!(cond)) {                                                           \
            std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": expected true: " #cond \
                       << "\n";                                                  \
            ++minitest::Registry::failures();                                    \
        }                                                                        \
    } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                                          \
    do {                                                                         \
        ++minitest::Registry::assertions();                                      \
        auto va = (a);                                                           \
        auto vb = (b);                                                           \
        if (!(va == vb)) {                                                       \
            std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": expected " #a \
                       << " == " #b << " (" << va << " vs " << vb << ")\n";      \
            ++minitest::Registry::failures();                                    \
        }                                                                        \
    } while (0)

#define EXPECT_NEAR(a, b, eps)                                                   \
    do {                                                                         \
        ++minitest::Registry::assertions();                                      \
        double va = (a);                                                         \
        double vb = (b);                                                         \
        if (std::fabs(va - vb) > (eps)) {                                        \
            std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": expected " #a \
                       << " ~= " #b << " (" << va << " vs " << vb << ")\n";      \
            ++minitest::Registry::failures();                                    \
        }                                                                        \
    } while (0)

#define EXPECT_THROW(stmt, exc_type)                                             \
    do {                                                                         \
        ++minitest::Registry::assertions();                                      \
        bool threw = false;                                                      \
        try {                                                                    \
            stmt;                                                                \
        } catch (const exc_type&) {                                              \
            threw = true;                                                        \
        } catch (...) {                                                          \
        }                                                                        \
        if (!threw) {                                                            \
            std::cerr << "  " << __FILE__ << ":" << __LINE__                     \
                       << ": expected " #stmt " to throw " #exc_type "\n";       \
            ++minitest::Registry::failures();                                    \
        }                                                                        \
    } while (0)
