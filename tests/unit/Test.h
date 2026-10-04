#pragma once
// A tiny test harness (no extra dependency).

#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace t {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& Registry() {
    static std::vector<Case> r;
    return r;
}

struct Register {
    Register(const char* name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
};

struct Failure {
    std::string message;
};

// A fresh empty temp folder for one test.
std::filesystem::path TempDir(const std::string& name);

}  // namespace t

#define T_CAT2(a, b) a##b
#define T_CAT(a, b) T_CAT2(a, b)
#define TEST(name)                                                          \
    static void T_CAT(test_, name)();                                       \
    static t::Register T_CAT(reg_, name)(#name, &T_CAT(test_, name));       \
    static void T_CAT(test_, name)()

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            std::ostringstream os_;                                                              \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed";                   \
            throw t::Failure{os_.str()};                                                         \
        }                                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                           \
    do {                                                                                         \
        auto va_ = (a);                                                                          \
        auto vb_ = (b);                                                                          \
        if (!(va_ == vb_)) {                                                                     \
            std::ostringstream os_;                                                              \
            os_ << __FILE__ << ":" << __LINE__ << ": " #a " == " #b " failed: [" << va_ << "] vs [" \
                << vb_ << "]";                                                                   \
            throw t::Failure{os_.str()};                                                         \
        }                                                                                        \
    } while (0)
