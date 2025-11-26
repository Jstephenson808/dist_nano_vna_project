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

void test_configure_serial_settings_correct() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
}

void test_restore_serial_settings_correct() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
}

void test_close_and_reset_all_targets() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking restore_serial");
}
void test_close_and_reset_all_VNA_COUNT() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking restore_serial");
}

void test_write_command() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
}

void test_read_exact_reads_one_byte() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
}

void test_find_binary_header_handles_command_prompt() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
}
void test_find_binary_header_handles_random_data() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
}
void test_find_binary_header_fails_gracefully() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_Numbers_Exist);
    RUN_TEST(test_Constants_Are_Valid);
    RUN_TEST(test_configure_serial_settings_correct);
    RUN_TEST(test_restore_serial_settings_correct);
    RUN_TEST(test_close_and_reset_all_targets);
    RUN_TEST(test_close_and_reset_all_VNA_COUNT);
    RUN_TEST(test_write_command);
    RUN_TEST(test_read_exact_reads_one_byte);
    RUN_TEST(test_find_binary_header_handles_command_prompt);
    RUN_TEST(test_find_binary_header_handles_random_data);
    RUN_TEST(test_find_binary_header_fails_gracefully);

    return UNITY_END();
}