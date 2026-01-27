#include "VnaCommunication.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

int vna_mocked = 0;
int num_mocked;
char **mock_ports;

int test_vna_count = 0;
int* test_fds = NULL;
struct termios* test_initial_port_settings = NULL;

int open_test_ports() {
    // Reset VNA_COUNT for clean state on subsequent runs
    test_vna_count = 0;
    
    // Initialise global variables
    test_fds = calloc(num_mocked, sizeof(int));
    test_initial_port_settings = calloc(num_mocked, sizeof(struct termios));
    
    if (!test_fds || !test_initial_port_settings) {
        fprintf(stderr, "Failed to allocate memory for serial port arrays\n");
        if (test_fds) {free(test_fds);}
        if (test_initial_port_settings) {free(test_initial_port_settings);}
        return -1;
    }

    for (int i = 0; i < num_mocked; i++) {
        test_fds[i] = open_serial(mock_ports[i]);
        if (test_fds[i] < 0)
            fprintf(stderr, "Failed to open serial port for test\n");
        if (configure_serial(test_fds[i],&test_initial_port_settings[i]) != 0)
            fprintf(stderr, "Error configuring port for test\n");
        test_vna_count++;
    }
    return test_fds[0];
}

void close_test_ports() {
    for (int i = 0; i < test_vna_count; i++) {
        tcflush(test_fds[i],TCIOFLUSH);
        restore_serial(test_fds[i],&test_initial_port_settings[i]);
        close(test_fds[i]);
    }
    free(test_fds);
    test_fds = NULL;
    free(test_initial_port_settings);
    test_initial_port_settings = NULL;
}

void setUp(void) {
    /* This is run before EACH TEST */
}

void tearDown(void) {
    /* This is run after EACH TEST */
    if (test_fds) {
        close_test_ports();
    }
}

/**
 * serial settings
 */
void test_configure_serial_settings_correct() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    TEST_IGNORE_MESSAGE("No idea how to compare huge termios structs");
    
    // change port settings to something random
    // change them back with configure_serial_settings
    // somehow needs to test that port settings are correct
    // restore
}
void test_restore_serial_settings_correct() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    TEST_IGNORE_MESSAGE("No idea how to compare huge termios structs");

    // change port settings to something random
    // change them back with restore_serial_settings
    // somehow needs to test that port settings are correct
}

void test_open_serial_mac_fallback_success(void) {
    #ifdef __APPLE__
        if (!vna_mocked) {
            TEST_IGNORE_MESSAGE("Cannot test fallback without physical device connected");
        }

        // Using a path that dooesn't exist on Mac
        const char *fake_linux_port = "/dev/ttyACM_FAKE";

        // Testing the normal function
        int raw_fd = open(fake_linux_port, O_RDWR | O_NOCTTY);
        TEST_ASSERT_LESS_THAN_INT_MESSAGE(0, raw_fd, "Sanity check: path should not exist on Mac");

        // Testing the dynamic function
        int smart_fd = open_serial(fake_linux_port);
        TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, smart_fd, "open_serial failed to automatically discover the connected VNA");

        // Clean up
        if (smart_fd >= 0) close(smart_fd);

    #else
        TEST_IGNORE_MESSAGE("Skipping Mac-specific test on non-Apple platform");
    #endif
}

void test_open_serial_fails_gracefully_on_bad_path(void) {
    // This path should fail because it doesn't exist and doesn't contain "ttyACM" hence the fallback won't trigger
    const char *bad_port = "/dev/ttyNONEXISTENT0";
    int fd = open_serial(bad_port);
    TEST_ASSERT_EQUAL_INT(-1, fd);
}

void test_write_command() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}

    int port = open_test_ports();
    write_command(port,"info\r");
    sleep(1);

    char *found_name = NULL;
    char buffer[100];
    int numBytes;
    do {
        numBytes = read(port,&buffer,sizeof(char)*100);
        if (numBytes < 0) {printf("Error reading: %s", strerror(errno));return;}
        found_name = strstr(buffer,"NanoVNA");
    } while (numBytes > 0 && !strstr(buffer,"ch>"));

    TEST_ASSERT_NOT_NULL(found_name);
}

/**
 * read_exact
 */
void test_read_exact_reads_one_byte() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    int port = open_test_ports();
    write_command(port,"info\r");
    sleep(1);

    uint8_t buffer;
    int bytes_read = read_exact(port,&buffer,sizeof(buffer));

    TEST_ASSERT_EQUAL_INT(1,bytes_read);
}
void test_read_exact_reads_ten_bytes() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    int port = open_test_ports();
    write_command(port,"info\r");
    sleep(1);

    uint8_t* buffer = calloc(sizeof(uint8_t),10);
    int bytes_read = read_exact(port,buffer,sizeof(uint8_t)*10);
    free(buffer);

    TEST_ASSERT_EQUAL_INT(10,bytes_read);
}

/**
 * test_vna
 */
void test_test_vna_success() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    int port = open_test_ports();
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,test_vna(port));
}

/**
 * in_vna_list
 */
extern char **vna_file_paths;
extern int total_connected_vnas;
void test_in_vna_list_true() {
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);
    for (int i = 0; i < 3; i++) {
        vna_file_paths[i] = calloc(sizeof(char),MAXIMUM_VNA_PATH_LENGTH);
        strncpy(vna_file_paths[i],"/dev/ttyACM10",MAXIMUM_VNA_PATH_LENGTH);
        total_connected_vnas++;
    }
    const char* actual_port = "/dev/ttyACM20";
    strncpy(vna_file_paths[1],actual_port,MAXIMUM_VNA_PATH_LENGTH);

    TEST_ASSERT_EQUAL_INT(1,in_vna_list(actual_port));

    // restore for next
    for (int i = 0; i < 3; i++) {
        free(vna_file_paths[i]);
        total_connected_vnas--;
    }
    free(vna_file_paths);
    vna_file_paths = NULL;
}
void test_in_vna_list_false() {
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);
    for (int i = 0; i < 3; i++) {
        vna_file_paths[i] = calloc(sizeof(char),MAXIMUM_VNA_PATH_LENGTH);
        strncpy(vna_file_paths[i],"/dev/ttyACM10",MAXIMUM_VNA_PATH_LENGTH);
    }
    const char* actual_port = "/dev/ttyACM20";

    TEST_ASSERT_EQUAL_INT(0,in_vna_list(actual_port));

    // restore for next
    for (int i = 0; i < 3; i++) {
        free(vna_file_paths[i]);
        total_connected_vnas--;
    }
    free(vna_file_paths);
    vna_file_paths = NULL;
}
void test_in_vna_list_empty() {
    const char* actual_port = "/dev/ttyACM20";

    TEST_ASSERT_EQUAL_INT(0,in_vna_list(actual_port));
}

/**
 * add_vna
 */
void test_add_vna_adds() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);
    total_connected_vnas = 0;

    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, add_vna(mock_ports[0]));
    TEST_ASSERT_EQUAL_STRING(mock_ports[0],vna_file_paths[0]);

    total_connected_vnas = 0;
    free(vna_file_paths[0]);
    free(vna_file_paths);
    vna_file_paths = NULL;
}
void test_add_vna_fails_max_vnas() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);
    total_connected_vnas = MAXIMUM_VNA_PORTS;

    TEST_ASSERT_EQUAL_INT(1,add_vna(mock_ports[0]));

    total_connected_vnas = 0;
    free(vna_file_paths);
    vna_file_paths = NULL;
}
void test_add_vna_fails_max_path_length() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);

    char* long_path = "12345678912345678912345678";
    TEST_ASSERT_EQUAL_INT(2,add_vna(long_path));
    
    free(vna_file_paths);
    vna_file_paths = NULL;
}
void test_add_vna_fails_not_a_file() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);

    char* fake_file = "/not_a_real_file_name";
    TEST_ASSERT_EQUAL_INT(-1,add_vna(fake_file));
    
    free(vna_file_paths);
    vna_file_paths = NULL;
}
void test_add_vna_fails_already_connected() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);

    vna_file_paths[0] = calloc(sizeof(char),MAXIMUM_VNA_PATH_LENGTH);
    strncpy(vna_file_paths[0],mock_ports[0],MAXIMUM_VNA_PATH_LENGTH);
    total_connected_vnas = 1;

    TEST_ASSERT_EQUAL_INT(3,add_vna(mock_ports[0]));
    
    total_connected_vnas = 0;
    free(vna_file_paths);
    vna_file_paths = NULL;
}
void test_add_vna_fails_not_a_nanovna() {
    TEST_IGNORE_MESSAGE("Needs new non-vna serial simulator script");
}

/**
 * remove_vna
 */
void test_remove_vna_removes() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);

    vna_file_paths[0] = calloc(sizeof(char),MAXIMUM_VNA_PATH_LENGTH);
    strncpy(vna_file_paths[0],mock_ports[0],MAXIMUM_VNA_PATH_LENGTH);
    total_connected_vnas = 1;

    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,remove_vna(mock_ports[0]));
    
    free(vna_file_paths);
    vna_file_paths = NULL;
}
void test_remove_vna_no_such_connection() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    vna_file_paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);

    vna_file_paths[0] = calloc(sizeof(char),MAXIMUM_VNA_PATH_LENGTH);
    strncpy(vna_file_paths[0],"fake_port_name",MAXIMUM_VNA_PATH_LENGTH);
    total_connected_vnas = 1;

    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE,remove_vna(mock_ports[0]));
    
    free(vna_file_paths[0]);
    total_connected_vnas = 0;
    free(vna_file_paths);
    vna_file_paths = NULL;
}

/**
 * find_vnas
 */
void test_find_vnas_finds_one() {
    const char* filename = "ttyACM0";

    FILE *fptr;
    fptr = fopen(filename, "w");
    fclose(fptr); 

    char** found = calloc(sizeof(char *),MAXIMUM_VNA_PORTS);
    TEST_ASSERT_EQUAL_INT(1,find_vnas(found,"."));
    TEST_ASSERT_NOT_NULL(strstr(found[0],filename));

    free(found[0]);
    free(found);
    remove(filename);
}
void test_find_vnas_finds_zero() {
    char** found = calloc(sizeof(char *),MAXIMUM_VNA_PORTS);
    TEST_ASSERT_EQUAL_INT(0,find_vnas(found,"."));

    free(found);
}


// initialise_port_array

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 1) {
        // args for if using python simulator or not
        // if not, flag to skip serial tests
        vna_mocked = 1;
        num_mocked = argc - 1;
        mock_ports = (char **)&argv[1];
    }
    
    RUN_TEST(test_configure_serial_settings_correct);
    RUN_TEST(test_restore_serial_settings_correct);

    RUN_TEST(test_write_command);

    RUN_TEST(test_read_exact_reads_one_byte);
    RUN_TEST(test_read_exact_reads_ten_bytes);
    RUN_TEST(test_open_serial_mac_fallback_success);
    RUN_TEST(test_open_serial_fails_gracefully_on_bad_path);

    RUN_TEST(test_test_vna_success);

    RUN_TEST(test_in_vna_list_true);
    RUN_TEST(test_in_vna_list_false);
    RUN_TEST(test_in_vna_list_empty);

    RUN_TEST(test_add_vna_adds);
    RUN_TEST(test_add_vna_fails_max_vnas);
    RUN_TEST(test_add_vna_fails_max_path_length);
    RUN_TEST(test_add_vna_fails_not_a_file);
    RUN_TEST(test_add_vna_fails_already_connected);
    RUN_TEST(test_add_vna_fails_not_a_nanovna);

    RUN_TEST(test_remove_vna_removes);
    RUN_TEST(test_remove_vna_no_such_connection);

    RUN_TEST(test_find_vnas_finds_one);
    RUN_TEST(test_find_vnas_finds_zero);

    return UNITY_END();
}