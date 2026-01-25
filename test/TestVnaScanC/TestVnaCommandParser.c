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

extern long start;
extern long stop;
extern int nbr_scans;
extern int sweeps;
extern int pps;

void setUp(void) {
    /* This is run before EACH TEST */

    // reset to defaults before each test
    start = 50000000;
    stop = 900000000;
    nbr_scans = 5;
    sweeps = 1;
    pps = 101;
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

// Tests for the setter commands
void testSetStartValidUpdatesStart(void) {
    long old_stop = stop;
    int old_scans = nbr_scans;
    int old_sweeps = sweeps;
    int old_pps = pps;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_LONG(100000000L, start);

    TEST_ASSERT_EQUAL_LONG(old_stop, stop);
    TEST_ASSERT_EQUAL_INT(old_scans, nbr_scans);
    TEST_ASSERT_EQUAL_INT(old_sweeps, sweeps);
    TEST_ASSERT_EQUAL_INT(old_pps, pps);
}

void testSetStopValidUpdatesStop(void) {
    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_LONG(200000000L, stop);
}

void testSetScansValidUpdatesNbrScans(void) {
    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(7, nbr_scans);
}

void testSetSweepsValidUpdatesSweeps(void) {
    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(3, sweeps);
}

void testSetPointsValidUpdatesPps(void) {
    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(55, pps);
}

void testSetStartNonNumericDoesNotChange(void) {
    long old_start = start;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_LONG(old_start, start);
}

void testSetStopNonNumericDoesNotChange(void) {
    long old_stop = stop;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_LONG(old_stop, stop);
}

void testSetScansNonNumericDoesNotChange(void) {
    int old_scans = nbr_scans;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(old_scans, nbr_scans);
}

void testSetSweepsNonNumericDoesNotChange(void) {
    int old_sweeps = sweeps;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(old_sweeps, sweeps);
}

void testSetPointsOutOfRangeDoesNotChange(void) {
    int old_pps = pps;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(old_pps, pps);
}

void testSetStartNotLessThanStopDoesNotChange(void) {
    long old_start = start;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_LONG(old_start, start);
}

void testSetStopNotGreaterThanStartDoesNotChange(void) {
    long old_stop = stop;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_LONG(old_stop, stop);
}

void testSetStartMissingValueDoesNotChange(void) {
    long old_start = start;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_LONG(old_start, start);
}

void testSetStopMissingValueDoesNotChange(void) {
    long old_stop = stop;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_LONG(old_stop, stop);
}

void testSetScansMissingValueDoesNotChange(void) {
    int old_scans = nbr_scans;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(old_scans, nbr_scans);
}

void testSetSweepsMissingValueDoesNotChange(void) {
    int old_sweeps = sweeps;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(old_sweeps, sweeps);
}

void testSetPointsMissingValueDoesNotChange(void) {
    int old_pps = pps;

    TEST_ASSERT_EQUAL(0, readCommand());  
    TEST_ASSERT_EQUAL_INT(old_pps, pps);
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
    
    RUN_TEST(testSetStartValidUpdatesStart);
    RUN_TEST(testSetStopValidUpdatesStop);
    RUN_TEST(testSetScansValidUpdatesNbrScans);
    RUN_TEST(testSetSweepsValidUpdatesSweeps);
    RUN_TEST(testSetPointsValidUpdatesPps);

    RUN_TEST(testSetStartNonNumericDoesNotChange);
    RUN_TEST(testSetStopNonNumericDoesNotChange);
    RUN_TEST(testSetScansNonNumericDoesNotChange);
    RUN_TEST(testSetSweepsNonNumericDoesNotChange);
    RUN_TEST(testSetPointsOutOfRangeDoesNotChange);

    RUN_TEST(testSetStartNotLessThanStopDoesNotChange);
    RUN_TEST(testSetStopNotGreaterThanStartDoesNotChange);

    RUN_TEST(testSetStartMissingValueDoesNotChange);
    RUN_TEST(testSetStopMissingValueDoesNotChange);
    RUN_TEST(testSetScansMissingValueDoesNotChange);
    RUN_TEST(testSetSweepsMissingValueDoesNotChange);
    RUN_TEST(testSetPointsMissingValueDoesNotChange);
    
    RUN_TEST(testExitCommandReturnsOne);

    return UNITY_END();
}