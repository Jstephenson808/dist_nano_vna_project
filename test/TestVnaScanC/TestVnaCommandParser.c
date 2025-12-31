#include "VnaCommandParser.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

int vna_mocked = 0;
int numVNAs;
const char **mock_ports;

extern int *SERIAL_PORTS;
extern struct termios* INITIAL_PORT_SETTINGS;
extern int VNA_COUNT_GLOBAL;

extern SweepMode sweep_mode;

void setUp(void) {
    /* This is run before EACH TEST */
}

void tearDown(void) {
    /* This is run after EACH TEST */
}

void testReadCommandUsesSweepsModeNoArgs() {
    if (!vna_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked input and VNA");
    readCommand();
    TEST_ASSERT_EQUAL(sweep_mode, NUM_SWEEPS);
}
void testReadCommandUsesSweepsModeSweepsArgs() {
    if (!vna_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked input and VNA");
    readCommand();
    TEST_ASSERT_EQUAL(sweep_mode, NUM_SWEEPS);
}
void testReadCommandUsesTimeModeTimeArg() {
    if (!vna_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked input and VNA");
    readCommand();
    TEST_ASSERT_EQUAL(sweep_mode, TIME);
}
void testHelpCommandReturnsSuccess() {
    if (!vna_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked input and VNA");
    TEST_ASSERT_EQUAL(readCommand(), 0);
}
void testExitCommandReturnsOne() {
    if (!vna_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked input and VNA");
    TEST_ASSERT_EQUAL(readCommand(), 1);
}

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 1) {
        // args for if using python simulator or not
        // if not, flag to skip serial tests
        vna_mocked = 1;
        numVNAs = argc - 1;
        mock_ports = (const char **)&argv[1];
    }
    
    /*
     * these tests will only work in this order and with a matching order
     * file (testin.txt) being piped into stdin
     */
    RUN_TEST(testReadCommandUsesSweepsModeNoArgs);
    RUN_TEST(testReadCommandUsesSweepsModeSweepsArgs);
    RUN_TEST(testReadCommandUsesTimeModeTimeArg);
    RUN_TEST(testHelpCommandReturnsSuccess);
    RUN_TEST(testExitCommandReturnsOne);

    return UNITY_END();
}