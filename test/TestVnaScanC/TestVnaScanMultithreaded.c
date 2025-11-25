#include "VnaScanMultithreaded.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

/* Helper methods */

// Will check the runtime environment for a connected NanoVNA device
static int nanoVNA_avaliable(void) {
    struct stat st;
    return (stat("/dev/ttyACM0", &st) == 0);
}

// Creates a pipe helper for unit tests
static void make_pipe(int *read_fd, int *write_fd) {
    int fds[2];
    TEST_ASSERT_EQUAL(0, pipe(fds));
    *read_file_descriptor = file_descriptor[0];
    *write_file_descriptor = file_descriptor[1];
}



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