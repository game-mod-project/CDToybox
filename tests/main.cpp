#include "harness.h"

int g_failures = 0;

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

int main() {
    int total = 0;
    for (auto& tc : registry()) {
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", tc.name);
        tc.fn();
        std::printf(g_failures == before ? "[  OK  ] %s\n" : "[ FAIL ] %s\n",
                    tc.name);
        ++total;
    }
    std::printf("\n%d tests, %d failures\n", total, g_failures);
    return g_failures ? 1 : 0;
}
