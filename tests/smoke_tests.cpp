#include "harness.h"

TEST(harness_reports_success) {
    CHECK(true);
    CHECK_EQ(1 + 1, 2);
}
