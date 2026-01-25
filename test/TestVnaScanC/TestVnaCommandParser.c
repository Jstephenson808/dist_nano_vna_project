#include "VnaCommandParser.h"
#include "unity.h"
#include <string.h> // used for strncpy for helper command for setter tests

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

// Helper function to run set command with given input string
static void runSetCommand(const char *line) {
    static char buf[128];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0'; // for Null termination

    (void)strtok(buf, " \n"); // consume "set"
    set();
}

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

// Setter unit tests 

void testSetStartValidUpdatesStart(void) {
    long old_stop = stop;
    runSetCommand("set start 100000000\n");

    TEST_ASSERT_EQUAL_INT((UNITY_INT)100000000, (UNITY_INT)start);
    TEST_ASSERT_EQUAL_INT((UNITY_INT)old_stop,   (UNITY_INT)stop);
}

void testSetStopValidUpdatesStop(void) {
    long old_start = start;
    runSetCommand("set stop 200000000\n");
    TEST_ASSERT_EQUAL_INT((UNITY_INT)200000000, (UNITY_INT)stop);
    TEST_ASSERT_EQUAL_INT((UNITY_INT)old_start,  (UNITY_INT)start);
}

void testSetScansValidUpdatesNbrScans(void) {
    runSetCommand("set scans 7\n");
    TEST_ASSERT_EQUAL_INT(7, nbr_scans);
}

void testSetSweepsValidUpdatesSweeps(void) {
    runSetCommand("set sweeps 3\n");
    TEST_ASSERT_EQUAL_INT(3, sweeps);
}

void testSetPointsValidUpdatesPps(void) {
    runSetCommand("set points 55\n");
    TEST_ASSERT_EQUAL_INT(55, pps);
}

// Invalid input tests

void testSetStartNonNumericDoesNotChange(void) {
    long old_start = start;
    runSetCommand("set start abc\n");
    TEST_ASSERT_EQUAL_INT((UNITY_INT)old_start, (UNITY_INT)start);
}

void testSetStopNonNumericDoesNotChange(void) {
    long old_stop = stop;
    runSetCommand("set stop xyz\n");
    TEST_ASSERT_EQUAL_INT((UNITY_INT)old_stop, (UNITY_INT)stop);
}

void testSetScansNonNumericDoesNotChange(void) {
    int old_scans = nbr_scans;
    runSetCommand("set scans nope\n");
    TEST_ASSERT_EQUAL_INT(old_scans, nbr_scans);
}

void testSetSweepsNonNumericDoesNotChange(void) {
    int old_sweeps = sweeps;
    runSetCommand("set sweeps nope\n");
    TEST_ASSERT_EQUAL_INT(old_sweeps, sweeps);
}

void testSetPointsOutOfRangeDoesNotChange(void) {
    int old_pps = pps;
    runSetCommand("set points 999\n");
    TEST_ASSERT_EQUAL_INT(old_pps, pps);
}

// Constraint tests

void testSetStartNotLessThanStopDoesNotChange(void) {
    long old_start = start;
    // stop is 900000000 from setUp(), so start=900000000 is invalid (must be < stop)
    runSetCommand("set start 900000000\n");
    TEST_ASSERT_EQUAL_INT((UNITY_INT)old_start, (UNITY_INT)start);
}

void testSetStopNotGreaterThanStartDoesNotChange(void) {
    long old_stop = stop;
    // start is 50000000 from setUp(), so stop=50000000 is invalid (must be > start)
    runSetCommand("set stop 50000000\n");
    TEST_ASSERT_EQUAL_INT((UNITY_INT)old_stop, (UNITY_INT)stop);
}

// Missing value tests

void testSetStartMissingValueDoesNotChange(void) {
    long old_start = start;
    runSetCommand("set start\n");
    TEST_ASSERT_EQUAL_INT((UNITY_INT)old_start, (UNITY_INT)start);
}

void testSetStopMissingValueDoesNotChange(void) {
    long old_stop = stop;
    runSetCommand("set stop\n");
    TEST_ASSERT_EQUAL_INT((UNITY_INT)old_stop, (UNITY_INT)stop);
}

void testSetScansMissingValueDoesNotChange(void) {
    int old_scans = nbr_scans;
    runSetCommand("set scans\n");
    TEST_ASSERT_EQUAL_INT(old_scans, nbr_scans);
}

void testSetSweepsMissingValueDoesNotChange(void) {
    int old_sweeps = sweeps;
    runSetCommand("set sweeps\n");
    TEST_ASSERT_EQUAL_INT(old_sweeps, sweeps);
}

void testSetPointsMissingValueDoesNotChange(void) {
    int old_pps = pps;
    runSetCommand("set points\n");
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
    RUN_TEST(testExitCommandReturnsOne);
    
    // Setter tests do NOT consume stdin:
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
    

    return UNITY_END();
}