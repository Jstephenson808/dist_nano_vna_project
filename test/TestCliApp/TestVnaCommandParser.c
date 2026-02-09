#include "VnaCommandParser.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

int vnas_mocked = 0;
char **mock_ports;

void setUp(void) {
    /* This is run before EACH TEST */
    if (vnas_mocked) {
        initialise_port_array();
        for (int i = 0; i < vnas_mocked; i++) {
            add_vna(mock_ports[i]);
        }
    }
}

void tearDown(void) {
    /* This is run after EACH TEST */
    if (vnas_mocked)
        teardown_port_array();
}

/**
 * read_command
 */
void testHelpCommandReturnsSuccess() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked input and VNA");
    TEST_ASSERT_EQUAL(read_command(), 0);
}
void testExitCommandReturnsOne() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked input and VNA");
    TEST_ASSERT_EQUAL(read_command(), 1);
}

/**
 * get_vna_list_from_args
 */
void testGetVnaListFromArgs() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked VNA");
    
    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    char args[] = "1 0\n";
    char* tok = strtok(args, " \n");
    TEST_ASSERT_EQUAL_INT(2, get_vna_list_from_args(tok,vna_list));

    int expected[] = {1, 0};
    TEST_ASSERT_EQUAL_INT_ARRAY(expected,vna_list,2);

    free(vna_list);
}
void testGetVnaListFromArgsFailsNonInt() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked VNA");
    
    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    char args[] = "this string is not an integer\n";
    char* tok = strtok(args, " \n");
    TEST_ASSERT_EQUAL_INT(-1, get_vna_list_from_args(tok,vna_list));

    free(vna_list);
}
void testGetVnaListFromArgsFailsOutOfBoundsHigh() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked VNA");
    
    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    char args[] = "0 12 1\n";
    char* tok = strtok(args, " \n");
    TEST_ASSERT_EQUAL_INT(-1, get_vna_list_from_args(tok,vna_list));

    free(vna_list);
}
void testGetVnaListFromArgsFailsOutOfBoundsLow() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked VNA");
    
    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    char args[] = "-1 1 0\n";
    char* tok = strtok(args, " \n");
    TEST_ASSERT_EQUAL_INT(-1, get_vna_list_from_args(tok,vna_list));

    free(vna_list);
}
void testGetVnaListFromArgsSkipsNotConnected() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked VNA");
    
    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    char args[] = "1 2 0\n";
    char* tok = strtok(args, " \n");
    TEST_ASSERT_EQUAL_INT(2, get_vna_list_from_args(tok,vna_list));

    int expected[] = {1, 0};
    TEST_ASSERT_EQUAL_INT_ARRAY(expected,vna_list,2);

    free(vna_list);   
}
void testGetVnaListFromReturnsNoMoreThanMax() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("cannot test without mocked VNA");
    
    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    char args[] = "1 0 1 0 1 0 1 0 1 0 1 0 1\n";
    char* tok = strtok(args, " \n");
    TEST_ASSERT_EQUAL_INT(MAXIMUM_VNA_PORTS, get_vna_list_from_args(tok,vna_list));

    free(vna_list);
}

/**
 * calculate_resolution
 */
void testCalculateResolutionStandard() {
    int scans;
    int pps;
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,calculate_resolution(101,&scans,&pps));
    TEST_ASSERT_EQUAL_INT(1,scans);
    TEST_ASSERT_EQUAL_INT(101,pps);
}
void testCalculateResolutionDouble() {
    int scans;
    int pps;
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,calculate_resolution(202,&scans,&pps));
    TEST_ASSERT_EQUAL_INT(2,scans);
    TEST_ASSERT_EQUAL_INT(101,pps);
}
void testCalculateResolutionNegative() {
    int scans;
    int pps;
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE,calculate_resolution(-1,&scans,&pps));
}

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 1) {
        // args for if using python simulator or not
        // if not, flag to skip serial tests
        vnas_mocked = argc - 1;
        if (vnas_mocked)
            mock_ports = (char **)&argv[1];
        else
            fprintf(stderr,"no mocked vnas, some tests will be skipped");
    }
    
    /**
     * these tests will only work in this order and with a matching order
     * file (testin.txt) being piped into stdin
     */
    RUN_TEST(testHelpCommandReturnsSuccess);
    RUN_TEST(testExitCommandReturnsOne);

    /**
     * these tests do not require piped file
     */
    RUN_TEST(testGetVnaListFromArgs);
    RUN_TEST(testGetVnaListFromArgsFailsNonInt);
    RUN_TEST(testGetVnaListFromArgsFailsOutOfBoundsHigh);
    RUN_TEST(testGetVnaListFromArgsFailsOutOfBoundsLow);
    RUN_TEST(testGetVnaListFromArgsSkipsNotConnected);
    RUN_TEST(testGetVnaListFromReturnsNoMoreThanMax);

    RUN_TEST(testCalculateResolutionStandard);
    RUN_TEST(testCalculateResolutionDouble);
    RUN_TEST(testCalculateResolutionNegative);

    return UNITY_END();
}