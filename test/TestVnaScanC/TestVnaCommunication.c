#include "VnaCommunication.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

int vna_mocked = 0;
int numVNAs;
const char **mock_ports;

int test_vna_count = 0;
int* test_fds = NULL;
struct termios* test_initial_port_settings = NULL;

int open_test_ports() {
    // Reset VNA_COUNT for clean state on subsequent runs
    test_vna_count = 0;
    
    // Initialise global variables
    test_fds = calloc(numVNAs, sizeof(int));
    test_initial_port_settings = calloc(numVNAs, sizeof(struct termios));
    
    if (!test_fds || !test_initial_port_settings) {
        fprintf(stderr, "Failed to allocate memory for serial port arrays\n");
        if (test_fds) {free(test_fds);}
        if (test_initial_port_settings) {free(test_initial_port_settings);}
        return -1;
    }

    for (int i = 0; i < numVNAs; i++) {
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

// test_vna

// in_vna_list

// add_vna
//      - working
//      - too many vnas
//      - too long path
//      - not a file/dir
//      - already connected
//      - not a NanoVNA-H

// remove_vna
//      - working
//      - not on list

// find_vnas (use fake dir and vna files)
//      - working
//      - edges

// initialise_port_array

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 1) {
        // args for if using python simulator or not
        // if not, flag to skip serial tests
        vna_mocked = 1;
        numVNAs = argc - 1;
        mock_ports = (const char **)&argv[1];
    }
    
    RUN_TEST(test_configure_serial_settings_correct);
    RUN_TEST(test_restore_serial_settings_correct);
    RUN_TEST(test_write_command);
    RUN_TEST(test_read_exact_reads_one_byte);
    RUN_TEST(test_read_exact_reads_ten_bytes);

    return UNITY_END();
}