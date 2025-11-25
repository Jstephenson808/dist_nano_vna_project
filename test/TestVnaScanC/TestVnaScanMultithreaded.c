#include "VnaScanMultithreaded.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_

void setUp(void) {
    /* This is run before EACH TEST */
}

void tearDown(void) {
    /* This is run after EACH TEST */
}

void test_Numbers_Exist() {
    TEST_ASSERT_EQUAL(0, 2-(1+1));
}

// Test the constants POINT and MASK from header file

void test_Constants_Are_Valid() {
    TEST_ASSERT_EQUAL(101, POINTS);
    TEST_ASSERT_EQUAL(135, MASK);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_Numbers_Exist);
    RUN_TEST(test_Constants_Are_Valid);
    return UNITY_END();
}