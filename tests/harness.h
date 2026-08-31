#pragma once

#include <cstdio>
#include <vector>

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();
extern int g_failures;

#define TEST(name)                                                            \
    static void name();                                                       \
    static struct name##_reg_t {                                              \
        name##_reg_t() { registry().push_back({#name, name}); }               \
    } name##_reg_inst;                                                        \
    static void name()

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        auto _a = (a);                                                        \
        auto _b = (b);                                                        \
        if (!(_a == _b)) {                                                    \
            std::printf("    FAIL %s:%d  %s == %s\n", __FILE__, __LINE__,     \
                        #a, #b);                                              \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)
