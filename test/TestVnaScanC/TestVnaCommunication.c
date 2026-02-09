#include "VnaCommunication.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

int vnas_mocked = 0;
char **mock_ports;

extern char **vna_names;
extern int total_vnas;
extern int * vna_fds;
extern struct termios* vna_initial_settings;

void init_test_ports() {
    // Reset VNA_COUNT for clean state on subsequent runs
    vna_names = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);
    vna_fds = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    vna_initial_settings = calloc(sizeof(struct termios),MAXIMUM_VNA_PORTS);

    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++)
        vna_fds[i] = -1;
    
    total_vnas = 0;
}

void open_test_ports() {
    for (int i = 0; i < vnas_mocked; i++) {
       add_vna(mock_ports[i]);
    }
}

void close_test_ports() {
    for (int i = 0; i < total_vnas; i++)
        tcflush(vna_fds[i],TCIOFLUSH);
    if (vna_names)
        teardown_port_array();
}

void setUp(void) {
    /* This is run before EACH TEST */
    init_test_ports();
}

void tearDown(void) {
    /* This is run after EACH TEST */
    close_test_ports();
}

/**
 * serial settings
 */
void test_configure_serial_settings_correct() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    TEST_IGNORE_MESSAGE("No idea how to compare huge termios structs");
    
    // change port settings to something random
    // change them back with configure_serial_settings
    // somehow needs to test that port settings are correct
    // restore
}
void test_restore_serial_settings_correct() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    TEST_IGNORE_MESSAGE("No idea how to compare huge termios structs");

    // change port settings to something random
    // change them back with restore_serial_settings
    // somehow needs to test that port settings are correct
}

void test_open_serial_mac_fallback_success(void) {
    #ifdef __APPLE__
        if (!vna_mocked)
            TEST_IGNORE_MESSAGE("Cannot test fallback without physical device connected");

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
    struct termios restore_tty;
    int fd = open_serial(bad_port,&restore_tty);
    TEST_ASSERT_EQUAL_INT(-1, fd);
}

void test_write_command() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");

    open_test_ports();
    int vna_num = 0;

    write_command(vna_num,"info\r");
    sleep(1);

    char *found_name = NULL;
    char buffer[100];
    int numBytes;
    do {
        numBytes = read(vna_fds[vna_num],&buffer,sizeof(char)*100);
        if (numBytes < 0) {printf("Error reading: %s", strerror(errno));return;}
        found_name = strstr(buffer,"NanoVNA");
    } while (!found_name && (numBytes > 0) && (!strstr(buffer,"ch>")));

    TEST_ASSERT_NOT_NULL(found_name);
}

/**
 * read_exact
 */
void test_read_exact_reads_one_byte() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    open_test_ports();
    int vna_num = 0;
    write_command(vna_num,"info\r");
    sleep(1);

    uint8_t buffer;
    int bytes_read = read_exact(vna_num,&buffer,sizeof(buffer));

    TEST_ASSERT_EQUAL_INT(1,bytes_read);
}
void test_read_exact_reads_ten_bytes() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    open_test_ports();
    int vna_num = 0;
    write_command(vna_num,"info\r");
    sleep(1);

    uint8_t* buffer = calloc(sizeof(uint8_t),10);
    int bytes_read = read_exact(vna_num,buffer,sizeof(uint8_t)*10);
    free(buffer);

    TEST_ASSERT_EQUAL_INT(10,bytes_read);
}

/**
 * test_vna
 */
void test_test_vna_success() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    open_test_ports();

    int vna_num = 0;
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,test_vna(vna_num));
}

/**
 * in_vna_list
 */
void test_in_vna_list_true() {
    for (int i = 0; i < 3; i++) {
        vna_names[i] = calloc(sizeof(char),MAXIMUM_VNA_PATH_LENGTH);
        strncpy(vna_names[i],"/dev/ttyACM10",MAXIMUM_VNA_PATH_LENGTH);
        total_vnas++;
    }
    const char* actual_port = "/dev/ttyACM20";
    strncpy(vna_names[1],actual_port,MAXIMUM_VNA_PATH_LENGTH);

    TEST_ASSERT_EQUAL_INT(1,in_vna_list(actual_port));

    // restore for next
    for (int i = 0; i < 3; i++) {
        free(vna_names[i]);
        total_vnas--;
    }
}
void test_in_vna_list_false() {
    for (int i = 0; i < 3; i++) {
        vna_names[i] = calloc(sizeof(char),MAXIMUM_VNA_PATH_LENGTH);
        strncpy(vna_names[i],"/dev/ttyACM10",MAXIMUM_VNA_PATH_LENGTH);
    }
    const char* actual_port = "/dev/ttyACM20";

    TEST_ASSERT_EQUAL_INT(0,in_vna_list(actual_port));

    // restore for next
    for (int i = 0; i < 3; i++) {
        free(vna_names[i]);
        total_vnas--;
    }
}
void test_in_vna_list_empty() {
    const char* fake_port = "/dev/ttyACM20";

    TEST_ASSERT_EQUAL_INT(0,in_vna_list(fake_port));
}

/**
 * get_connected_vnas
 */
void test_get_connected_vnas() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    
    open_test_ports();
    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        vna_list[i] = -1;
    }

    TEST_ASSERT_EQUAL_INT(vnas_mocked, get_connected_vnas(vna_list));
    for (int i = 0; i < vnas_mocked; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL(0,vna_list[i]);
    }
    TEST_ASSERT_EQUAL_INT(-1,vna_list[vnas_mocked]);

    free(vna_list);
}
void test_get_connected_vnas_no_vnas() {
    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        vna_list[i] = -1;
    }

    TEST_ASSERT_EQUAL_INT(0, get_connected_vnas(vna_list));
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL(-1,vna_list[i]);
    }

    free(vna_list);
}

/**
 * add_vna
 */
void test_add_vna_adds() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");

    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, add_vna(mock_ports[0]));
    TEST_ASSERT_EQUAL_STRING(mock_ports[0],vna_names[0]);
}
void test_add_vna_fails_max_vnas() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    
    total_vnas = MAXIMUM_VNA_PORTS;
    TEST_ASSERT_EQUAL_INT(1,add_vna(mock_ports[0]));

    total_vnas = 0;
}
void test_add_vna_fails_max_path_length() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");

    char* long_path = "12345678912345678912345678";
    TEST_ASSERT_EQUAL_INT(2,add_vna(long_path));
}
void test_add_vna_fails_not_a_file() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");

    char* fake_file = "/not_a_real_file_name";
    TEST_ASSERT_EQUAL_INT(-1,add_vna(fake_file));
}
void test_add_vna_fails_already_connected() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    open_test_ports();

    TEST_ASSERT_EQUAL_INT(3,add_vna(mock_ports[0]));
}
void test_add_vna_fails_not_a_nanovna() {
    TEST_IGNORE_MESSAGE("Needs new non-vna serial simulator script");
}

/**
 * remove_vna_name
 */
void test_remove_vna_name_removes() {
    if (!vnas_mocked)   
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    open_test_ports();

    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,remove_vna_name(mock_ports[0]));
}
void test_remove_vna_name_no_such_connection() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    open_test_ports();

    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE,remove_vna_name("fake_port_name"));
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

void test_teardown_port_array_targets() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
    open_test_ports();

    TEST_ASSERT_NOT_EQUAL_INT(0,total_vnas);
    teardown_port_array();
    TEST_ASSERT_EQUAL_INT(0,total_vnas);
    // could also do with comparing the termios structures etc.
    TEST_ASSERT_NULL(vna_names);
    TEST_ASSERT_NULL(vna_fds);
    TEST_ASSERT_NULL(vna_initial_settings);
}

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 1) {
        // args for if using python simulator or not
        // if not, flag to skip serial tests
        vnas_mocked = argc - 1;
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

    RUN_TEST(test_get_connected_vnas);
    RUN_TEST(test_get_connected_vnas_no_vnas);

    RUN_TEST(test_add_vna_adds);
    RUN_TEST(test_add_vna_fails_max_vnas);
    RUN_TEST(test_add_vna_fails_max_path_length);
    RUN_TEST(test_add_vna_fails_not_a_file);
    RUN_TEST(test_add_vna_fails_already_connected);
    RUN_TEST(test_add_vna_fails_not_a_nanovna);

    RUN_TEST(test_remove_vna_name_removes);
    RUN_TEST(test_remove_vna_name_no_such_connection);

    RUN_TEST(test_find_vnas_finds_one);
    RUN_TEST(test_find_vnas_finds_zero);

    // initialise port array

    RUN_TEST(test_teardown_port_array_targets);

    return UNITY_END();
}