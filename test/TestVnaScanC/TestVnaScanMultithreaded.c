#include "VnaScanMultithreaded.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

int vna_mocked = 0;
int numVNAs;
const char **ports;

extern int *SERIAL_PORTS;
extern struct termios* INITIAL_PORT_SETTINGS;
extern int VNA_COUNT_GLOBAL;

void setUp(void) {
    /* This is run before EACH TEST */
    if (vna_mocked) {
        // Reset VNA_COUNT for clean state on subsequent runs
        VNA_COUNT_GLOBAL = 0;
        
        // Initialise global variables
        SERIAL_PORTS = calloc(numVNAs, sizeof(int));
        INITIAL_PORT_SETTINGS = calloc(numVNAs, sizeof(struct termios));
        
        if (!SERIAL_PORTS || !INITIAL_PORT_SETTINGS) {
            fprintf(stderr, "Failed to allocate memory for serial port arrays\n");
            if (SERIAL_PORTS) {free(SERIAL_PORTS);}
            if (INITIAL_PORT_SETTINGS) {free(INITIAL_PORT_SETTINGS);}
            return;
        }

        for (int i = 0; i < numVNAs; i++) {
            SERIAL_PORTS[i] = open_serial(ports[i]);
            if (SERIAL_PORTS[i] < 0) {
                fprintf(stderr, "Failed to open serial port for test\n");
                close_and_reset_all();
                free(SERIAL_PORTS);
                free(INITIAL_PORT_SETTINGS);
                SERIAL_PORTS = NULL;
                INITIAL_PORT_SETTINGS = NULL;
                VNA_COUNT_GLOBAL=0;
                return;
            }
            if (configure_serial(SERIAL_PORTS[i],&INITIAL_PORT_SETTINGS[i]) != 0) {
                fprintf(stderr, "Error configuring port for test\n");
                if (SERIAL_PORTS[i] >= 0)
                    close(SERIAL_PORTS[i]);
                close_and_reset_all();
                free(SERIAL_PORTS);
                free(INITIAL_PORT_SETTINGS);
                SERIAL_PORTS = NULL;
                INITIAL_PORT_SETTINGS = NULL;
                VNA_COUNT_GLOBAL=0;
                return;
            }
            VNA_COUNT_GLOBAL++;
        }
    } else {
        // pretend to have one VNA for buffer functions
        VNA_COUNT_GLOBAL = 1;
    }
}

void tearDown(void) {
    /* This is run after EACH TEST */
    if (vna_mocked) {
        for (int i = 0; i < VNA_COUNT_GLOBAL; i++)
            tcflush(SERIAL_PORTS[i],TCIOFLUSH);
        close_and_reset_all();
        free(SERIAL_PORTS);
        SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS);
        INITIAL_PORT_SETTINGS = NULL;
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
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
    
    TEST_ASSERT_NOT_EQUAL_INT(0,VNA_COUNT_GLOBAL);

    close_and_reset_all();

    TEST_ASSERT_EQUAL_INT(0,VNA_COUNT_GLOBAL);
    // could also do with comparing the termios structures
}

void test_write_command() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}

    int port = SERIAL_PORTS[0];
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
    int port = SERIAL_PORTS[0];
    write_command(port,"info\r");
    sleep(1);

    uint8_t buffer;
    int bytes_read = read_exact(port,&buffer,sizeof(buffer));

    TEST_ASSERT_EQUAL_INT(1,bytes_read);
}

void test_find_binary_header_handles_random_data() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read()");}
    int port = SERIAL_PORTS[0];
    char msg_buff[100];
    int start = 50000000;
    int step = 1000;
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, start+(step*(POINTS-1)), POINTS, MASK);
    
    write_command(port,"info\r");
    sleep(1);
    write_command(port,msg_buff);
    sleep(1);

    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(port,&fp,MASK,POINTS);
    TEST_ASSERT_EQUAL_INT(0,error);

    uint32_t freq;
    int bytes_read = read_exact(port,(uint8_t*)&freq,sizeof(uint8_t)*4);
    TEST_ASSERT_EQUAL_INT(4,bytes_read);
    TEST_ASSERT_EQUAL_INT(start+step,freq);
}
void test_find_binary_header_constructs_correct_first_point() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read()");}
    int port = SERIAL_PORTS[0];
    char msg_buff[100];
    int start = 50000000;
    int step = 1000;
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, start+(step*(POINTS-1)), POINTS, MASK);
    
    write_command(port,msg_buff);
    sleep(1);

    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(port,&fp,MASK,POINTS);
    TEST_ASSERT_EQUAL_INT(0,error);

    TEST_ASSERT_EQUAL_INT(start,fp.frequency);
}
void test_find_binary_header_fails_gracefully() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read()");}
    int port = SERIAL_PORTS[0];
    char msg_buff[100];
    int start = 50000000;
    int step = 1000;
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, start+(step*(POINTS-1)), POINTS, MASK);
    
    write_command(port,msg_buff);
    sleep(1);

    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(port,&fp,MASK,POINTS);
    TEST_ASSERT_EQUAL_INT(0,error);
    error = find_binary_header(port,&fp,MASK,POINTS);
    TEST_ASSERT_NOT_EQUAL_INT(0,error);
}

/**
 * Bounded Buffer create/destroy
 */
void test_create_bounded_buffer() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    int error = create_bounded_buffer(b);
    TEST_ASSERT_EQUAL(0,error);
    TEST_ASSERT_NOT_NULL(b->buffer);
    destroy_bounded_buffer(b);
}

/**
 * Bounded buffer add
 */
void test_add_buff_adds() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(b);
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
    create_bounded_buffer(b);
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
    create_bounded_buffer(b);
    b->count = N;
    b->buffer[b->in] = NULL;

    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    struct thread_imitator_add_args args = {b,data};
    pthread_t thread;
    int error = pthread_create(&thread, NULL, &thread_imitator_add, &args);
    if(error != 0){
        fprintf(stderr, "Error %i creating thread for test_add_buff_escapes_block_after_full(): %s\n", errno, strerror(errno));
        return;
    }
    sleep(1);
    int inner_in = b->in;
    int inner_count = b->count;

    b->count--;
    pthread_cond_signal(&b->take_cond);

    pthread_join(thread,NULL);

    TEST_ASSERT_EQUAL_INT(0,inner_in);
    TEST_ASSERT_EQUAL_INT(N,inner_count);
    TEST_ASSERT_EQUAL_INT(1,b->in);
    TEST_ASSERT_EQUAL_INT(N,b->count);
    TEST_ASSERT_NOT_NULL(b->buffer[b->out]);
    free(data);
    destroy_bounded_buffer(b);
}

/**
 * Bounded Buffer take
 */
void test_take_buff_takes() {
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(b);
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
    create_bounded_buffer(b);
    b->out = N-1;
    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    b->count = 1;
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
    create_bounded_buffer(b);
    b->count = 0;

    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    b->buffer[b->out] = data;

    pthread_t thread;
    int error = pthread_create(&thread, NULL, &thread_imitator_take, b);
    if(error != 0){
        fprintf(stderr, "Error %i creating thread for test_take_buff_escapes_block_after_full(): %s\n", errno, strerror(errno));
        return;
    }
    sleep(1);
    int inner_out = b->out;
    int inner_count = b->count;

    b->count++;
    pthread_cond_signal(&b->add_cond);

    pthread_join(thread,NULL);

    TEST_ASSERT_EQUAL_INT(0,inner_out);
    TEST_ASSERT_EQUAL_INT(0,inner_count);
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
    int port = SERIAL_PORTS[0];
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
    int port = SERIAL_PORTS[0];
    int start = 50000000;
    write_command(port,"malform\r");
    sleep(1);
    
    struct datapoint_nanoVNA_H* data = pull_scan(port,1,start,start+(POINTS*100000));
    TEST_ASSERT_NULL(data);
    sleep(2);
}
void test_producer_num_takes_correct_points() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Requires mocking pull_scan()");}
    int port = SERIAL_PORTS[0];
    int start = 50000000;
    
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(b);

    int scans = 2;
    int size = ((scans*POINTS-1)*100000);
    int step = size / (scans*POINTS-1);

    struct scan_producer_args args;
    args.vna_id = 1;
    args.serial_port = port;
    args.nbr_scans = scans;
    args.start = start;
    args.stop = start+size;
    args.nbr_sweeps = 1; 
    args.bfr = b;
    scan_producer_num(&args);
    for (int scan = 0; scan < scans; scan++) {
        for (int i = 0; i < POINTS; i++) {
            int expected = start+((scan*POINTS + i)*step);
            TEST_ASSERT_EQUAL_INT(expected,b->buffer[scan]->point[i].frequency);
        }
    }
    destroy_bounded_buffer(b);
}
void test_producer_time_takes_correct_time() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Requires mocking pull_scan()");}
    int port = SERIAL_PORTS[0];
    int start = 50000000;
    
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(b);

    int scans = 2;
    int size = ((scans*POINTS-1)*100000);
    int time_to_scan = 2;

    struct scan_producer_args scan_args;
    scan_args.vna_id = 1;
    scan_args.serial_port = port;
    scan_args.nbr_scans = scans;
    scan_args.start = start;
    scan_args.stop = start+size;
    scan_args.nbr_sweeps = time_to_scan; 
    scan_args.bfr = b;

    struct scan_timer_args time_args;
    time_args.time_to_wait = time_to_scan;
    time_args.b = b;

    struct timeval start_time, end_time;
    pthread_t thread;

    gettimeofday(&start_time, NULL);
    int error = pthread_create(&thread, NULL, &scan_timer, &time_args);
    if(error != 0){
        fprintf(stderr, "Error %i creating timer thread for test_producer_time_takes_correct_time(): %s\n", errno, strerror(errno));
        return;
    }
    scan_producer_time(&scan_args);
    gettimeofday(&end_time,NULL);

    int time_expired = end_time.tv_sec-start_time.tv_sec;
    TEST_ASSERT_GREATER_OR_EQUAL_INT(time_to_scan,time_expired);
    TEST_ASSERT_LESS_OR_EQUAL_INT(time_to_scan+1,time_expired);

    destroy_bounded_buffer(b);
}

/**
 * Consumer & Helpers
 */
void test_consumer_constructs_valid_output() {
    TEST_IGNORE_MESSAGE("Requires mocking printf()");
    BoundedBuffer *b = malloc(sizeof(BoundedBuffer));
    create_bounded_buffer(b);

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
    RUN_TEST(test_producer_num_takes_correct_points);
    RUN_TEST(test_producer_time_takes_correct_time);
    RUN_TEST(test_consumer_constructs_valid_output);

    return UNITY_END();
}