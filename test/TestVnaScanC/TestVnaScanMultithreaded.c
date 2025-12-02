#include "VnaScanMultithreaded.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

int vna_mocked = 0;
int numVNAs;
const char **ports;

int *SERIAL_PORTS_TEST;
struct termios *INITIAL_PORT_SETTINGS_TEST;

atomic_int VNA_COUNT_TEST = 0;

void setUp(void) {
    /* This is run before EACH TEST */
    if (vna_mocked) {
        // Reset VNA_COUNT for clean state on subsequent runs
        VNA_COUNT_TEST = 0;
        
        // Initialise global variables
        SERIAL_PORTS_TEST = calloc(numVNAs, sizeof(int));
        INITIAL_PORT_SETTINGS_TEST = calloc(numVNAs, sizeof(struct termios));
        
        if (!SERIAL_PORTS_TEST || !INITIAL_PORT_SETTINGS_TEST) {
            fprintf(stderr, "Failed to allocate memory for serial port arrays\n");
            free(SERIAL_PORTS_TEST);
            free(INITIAL_PORT_SETTINGS_TEST);
            return;
        }

        for (int i = 0; i < numVNAs; i++) {
            SERIAL_PORTS_TEST[i] = open_serial(ports[i]);
            if (SERIAL_PORTS_TEST[i] < 0) {
                fprintf(stderr, "Failed to open serial port for test\n");
                for (int j = 0; j < i; j++) {
                    if (SERIAL_PORTS_TEST[j] >= 0) {
                        close(SERIAL_PORTS_TEST[j]);
                    }
                }
                free(SERIAL_PORTS_TEST);
                free(INITIAL_PORT_SETTINGS_TEST);
                SERIAL_PORTS_TEST = NULL;
                INITIAL_PORT_SETTINGS_TEST = NULL;
                return;
            }
            if (configure_serial(SERIAL_PORTS_TEST[i],&INITIAL_PORT_SETTINGS_TEST[i]) != 0) {
                fprintf(stderr, "Error configuring port for test\n");
                for (int j = 0; j <= i; j++) {
                    if (SERIAL_PORTS_TEST[j] >= 0) {
                        close(SERIAL_PORTS_TEST[j]);
                    }
                }
                free(SERIAL_PORTS_TEST);
                free(INITIAL_PORT_SETTINGS_TEST);
                SERIAL_PORTS_TEST = NULL;
                INITIAL_PORT_SETTINGS_TEST = NULL;
                return;
            }
            VNA_COUNT_TEST++;
        }
    }
}

void tearDown(void) {
    /* This is run after EACH TEST */
    if (vna_mocked) {
        for (int i = VNA_COUNT_TEST-1; i >= 0; i--) {
            tcflush(SERIAL_PORTS_TEST[i],TCIOFLUSH);
            restore_serial(SERIAL_PORTS_TEST[i], &INITIAL_PORT_SETTINGS_TEST[i]);
            close(SERIAL_PORTS_TEST[i]);
            VNA_COUNT_TEST--;
        }
        free(SERIAL_PORTS_TEST);
        SERIAL_PORTS_TEST = NULL;
        free(INITIAL_PORT_SETTINGS_TEST);
        INITIAL_PORT_SETTINGS_TEST = NULL;
    }
}

/**
 * Serial functions
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

void test_close_and_reset_all_targets() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Requires mocking restore_serial and global variables");}
    TEST_IGNORE_MESSAGE("Requires mocking global variables");
}
void test_close_and_reset_all_VNA_COUNT() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Requires mocking restore_serial and global variables");}
    TEST_IGNORE_MESSAGE("Requires mocking global variables");
}

void test_write_command() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}

    int port = SERIAL_PORTS_TEST[0];
    write_command(port,"info\r");
    sleep(5);

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
    int port = SERIAL_PORTS_TEST[0];
    write_command(port,"info\r");
    sleep(5);

    uint8_t buffer;
    int bytes_read = read_exact(port,&buffer,sizeof(buffer));

    TEST_ASSERT_EQUAL_INT(1,bytes_read);
}

void test_find_binary_header_handles_random_data() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read()");}
    int port = SERIAL_PORTS_TEST[0];
    char msg_buff[100];
    int start = 50000000;
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, start+10000000, POINTS, MASK);
    write_command(port,"info\r");
    sleep(2);
    write_command(port,msg_buff);
    sleep(5);

    int error = find_binary_header(port,MASK,POINTS);
    TEST_ASSERT_EQUAL_INT(0,error);

    uint32_t freq;
    int bytes_read = read_exact(port,(uint8_t*)&freq,sizeof(uint8_t)*4);
    TEST_ASSERT_EQUAL_INT(4,bytes_read);
    TEST_ASSERT_EQUAL_INT(start,freq);
}
void test_find_binary_header_fails_gracefully() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read()");}
    int port = SERIAL_PORTS_TEST[0];
    char msg_buff[100];
    int start = 50000000;
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, start+10000000, POINTS, MASK);
    write_command(port,msg_buff);
    sleep(5);

    int error = find_binary_header(port,MASK,POINTS);
    TEST_ASSERT_EQUAL_INT(0,error);
    error = find_binary_header(port,MASK,POINTS);
    TEST_ASSERT_NOT_EQUAL_INT(0,error);
}

/**
 * Bounded Buffer create/destroy
 */
void test_create_bounded_buffer() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    int error = create_bounded_buffer(5,b);
    TEST_ASSERT_EQUAL(0,error);
    TEST_ASSERT_NOT_NULL(b->buffer);
    destroy_bounded_buffer(b);
}

/**
 * Bounded buffer add
 */
void test_add_buff_adds() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(5,b);
    TEST_ASSERT_EQUAL_INT(0,b->in);
    TEST_ASSERT_EQUAL_INT(0,b->count);
    b->buffer[b->in] = NULL;
    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    add_buff(b,data);
    TEST_ASSERT_EQUAL_INT(1,b->in);
    TEST_ASSERT_EQUAL_INT(1,b->count);
    TEST_ASSERT_NOT_NULL(b->buffer[b->out]);
    free(data);
    destroy_bounded_buffer(b);
}
void test_add_buff_cycles() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(N,b);
    b->in = N-1;
    b->buffer[b->in] = NULL;
    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    add_buff(b,data);
    TEST_ASSERT_EQUAL_INT(0,b->in);
    TEST_ASSERT_NOT_NULL(b->buffer[N-1]);
    free(data);
    destroy_bounded_buffer(b);
}
struct thread_imitator_add_args {
    BoundedBuffer *b;
    struct datapoint_nanoVNA_H *data;
};
void* thread_imitator_add(void *arguments) {
    struct thread_imitator_add_args *args = arguments;
    add_buff(args->b,args->data);
    return NULL;
}
void test_add_buff_escapes_block_after_full() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(5,b);
    b->count = 5;
    b->buffer[b->in] = NULL;

    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    struct thread_imitator_add_args args = {b,data};
    pthread_t thread;
    int error = pthread_create(&thread, NULL, &thread_imitator_add, &args);
    if(error != 0){
        fprintf(stderr, "Error %i creating thread for test_add_buff_escapes_block_after_full(): %s\n", errno, strerror(errno));
        return;
    }

    b->count = 4;
    pthread_cond_signal(&b->add_cond);

    pthread_join(thread,NULL);

    TEST_ASSERT_EQUAL_INT(1,b->in);
    TEST_ASSERT_EQUAL_INT(5,b->count);
    TEST_ASSERT_NOT_NULL(b->buffer[b->out]);
    free(data);
    destroy_bounded_buffer(b);
}

/**
 * Bounded Buffer take
 */
void test_take_buff_takes() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(5,b);
    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    b->buffer[b->out] = data;
    b->count = 1;
    struct datapoint_nanoVNA_H *dataOut = take_buff(b);
    TEST_ASSERT_EQUAL_INT(1,b->out);
    TEST_ASSERT_EQUAL_INT(0,b->count);
    TEST_ASSERT_NOT_NULL(dataOut);
    free(data);
    destroy_bounded_buffer(b);
}
void test_take_buff_cycles() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(N,b);
    b->out = N-1;
    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    struct datapoint_nanoVNA_H *dataOut = take_buff(b);
    TEST_ASSERT_EQUAL_INT(0,b->out);
    TEST_ASSERT_NOT_NULL(dataOut);
    free(data);
    destroy_bounded_buffer(b);
}
void* thread_imitator_take(void *arguments) {
    struct BoundedBuffer *b = arguments;
    take_buff(b);
    return NULL;
}
void test_take_buff_escapes_block_after_full() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(5,b);
    b->count = 0;

    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    b->buffer[b->out] = data;

    pthread_t thread;
    int error = pthread_create(&thread, NULL, &thread_imitator_take, b);
    if(error != 0){
        fprintf(stderr, "Error %i creating thread for test_take_buff_escapes_block_after_full(): %s\n", errno, strerror(errno));
        return;
    }

    b->count = 1;
    pthread_cond_signal(&b->take_cond);

    pthread_join(thread,NULL);

    TEST_ASSERT_EQUAL_INT(1,b->out);
    TEST_ASSERT_EQUAL_INT(0,b->count);
    free(data);
    destroy_bounded_buffer(b);
}

/**
 * Producer & Helpers
 */
void test_pull_scan_constructs_valid_data() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read_exact()");}
    int port = SERIAL_PORTS_TEST[0];
    int start = 50000000;
    
    struct datapoint_nanoVNA_H* data = pull_scan(port,1,start,start+(POINTS*100000));

    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_INT(1,data->vna_id);
    TEST_ASSERT_NOT_NULL(data->point);
    for (int i = 0; i < POINTS; i++) {
        TEST_ASSERT_EQUAL_INT(start+(i*POINTS*1000),data->point[i].frequency);
    }
}
void test_pull_scan_nulls_malformed_data() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read_exact()");}
    int port = SERIAL_PORTS_TEST[0];
    int start = 50000000;
    write_command(port,"malform\r");
    sleep(2);
    
    struct datapoint_nanoVNA_H* data = pull_scan(port,1,start,start+(POINTS*100000));
    TEST_ASSERT_NULL(data);
}
void test_producer_takes_correct_points() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Requires mocking pull_scan()");}
    TEST_IGNORE_MESSAGE("currently fails (w/ segfault) because of timing issues with pipeline");
    int port = SERIAL_PORTS_TEST[0];
    int start = 50000000;
    
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(2,b);

    struct scan_producer_args args;
    args.vna_id = 1;
    args.serial_port = port;
    args.nbr_scans = 2;
    args.start = start;
    args.stop = start+(2*POINTS*100000);
    args.nbr_sweeps = 1; 
    args.bfr = b;
    scan_producer(&args);
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < POINTS; j++) {
            TEST_ASSERT_EQUAL_INT(start+(j*POINTS*1000)+(i*POINTS*POINTS*1000),
                                    b->buffer[i]->point[j].frequency);
        }
    }
    destroy_bounded_buffer(b);
}

/**
 * Consumer & Helpers
 */
void test_consumer_constructs_valid_output() {
    TEST_IGNORE_MESSAGE("Requires mocking printf()");
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(5,b);

    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    struct timeval time;
    gettimeofday(&time, NULL);

    data->vna_id = 1;
    data->send_time = time;
    data->receive_time = time;
    for (int i = 0; i < POINTS; i++) {
        data->point[i] = (struct nanovna_raw_datapoint) {0,{0,0},{0,0}};
    }

    b->buffer[b->out] = data;
    b->count = 1;

    b->complete=0;

    struct scan_consumer_args args = {b};
    scan_consumer(&args);

    // CHECK OUTPUT CORRECT (I'll figure out how later)

    TEST_ASSERT_EQUAL_INT(0,b->count);
    free(data);
    destroy_bounded_buffer(b);
}

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 1) {
        // args for if using python simulator or not
        // if not, flag to skip serial tests
        vna_mocked = 1;
        numVNAs = argc - 1;
        ports = (const char **)&argv[1];
    }
    
    // serial tests
    RUN_TEST(test_configure_serial_settings_correct);
    RUN_TEST(test_restore_serial_settings_correct);
    RUN_TEST(test_close_and_reset_all_targets);
    RUN_TEST(test_close_and_reset_all_VNA_COUNT);
    RUN_TEST(test_write_command);
    RUN_TEST(test_read_exact_reads_one_byte);
    RUN_TEST(test_find_binary_header_handles_random_data);
    RUN_TEST(test_find_binary_header_fails_gracefully);

    // bounded buffer tests
    RUN_TEST(test_create_bounded_buffer);
    RUN_TEST(test_add_buff_adds);
    RUN_TEST(test_add_buff_cycles);
    RUN_TEST(test_add_buff_escapes_block_after_full);
    RUN_TEST(test_take_buff_takes);
    RUN_TEST(test_take_buff_cycles);
    RUN_TEST(test_take_buff_escapes_block_after_full);

    // producer/consumer tests
    RUN_TEST(test_pull_scan_constructs_valid_data);
    RUN_TEST(test_pull_scan_nulls_malformed_data);
    RUN_TEST(test_producer_takes_correct_points);
    RUN_TEST(test_consumer_constructs_valid_output);

    return UNITY_END();
}