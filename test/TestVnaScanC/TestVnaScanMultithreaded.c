#include "VnaScanMultithreaded.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

void setUp(void) {
    /* This is run before EACH TEST */
}

void tearDown(void) {
    /* This is run after EACH TEST */
}

void test_Numbers_Exist() {
    TEST_ASSERT_EQUAL(0, 2-(1+1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_Numbers_Exist);
    return UNITY_END();
}