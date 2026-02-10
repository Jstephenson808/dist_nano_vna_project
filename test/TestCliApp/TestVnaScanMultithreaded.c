#include "VnaScanMultithreaded.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

#define PPS 101

int vnas_mocked = 0;
char **mock_ports;

/**
 * externs from VnaScanMultithreaded, for testing
 */
extern int* scan_states;
extern pthread_t* scan_threads;

/**
 * extern from VnaCommunication, purely for flushing I/O
 */
extern int* vna_fds;

void setUp(void) {
    /* This is run before EACH TEST */
    if (vnas_mocked) {
        initialise_port_array();
        for (int i = 0; i < vnas_mocked; i++) {
            add_vna(mock_ports[i]);
        }
        for (int i = 0; i < vnas_mocked; i++) {
            tcflush(vna_fds[i],TCIOFLUSH);
        }
    }
}

void tearDown(void) {
    /* This is run after EACH TEST */
    if (vnas_mocked)
        teardown_port_array();
    if (scan_states) {
        free(scan_states);
        scan_states = NULL;
    }
    if (scan_threads) {
        free(scan_threads);
        scan_threads = NULL;
    }
}

/**
 * Bounded Buffer create/destroy
 */
void test_create_bounded_buffer() {
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    int error = create_bounded_buffer(b,PPS);
    TEST_ASSERT_EQUAL(0,error);
    TEST_ASSERT_NOT_NULL(b->buffer);
    destroy_bounded_buffer(b);
}

/**
 * Bounded buffer add
 */
void test_add_buff_adds() {
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);
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
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);
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
    struct bounded_buffer *b;
    struct datapoint_nanoVNA_H *data;
};
void* thread_imitator_add(void *arguments) {
    struct thread_imitator_add_args *args = arguments;
    add_buff(args->b,args->data);
    return NULL;
}
void test_add_buff_escapes_block_after_full() {
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);
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
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);
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
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);
    b->out = N-1;
    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    b->buffer[b->out] = data;
    b->count = 1;
    struct datapoint_nanoVNA_H *dataOut = take_buff(b);
    TEST_ASSERT_EQUAL_INT(0,b->out);
    TEST_ASSERT_NOT_NULL(dataOut);
    free(data);
    destroy_bounded_buffer(b);
}
void* thread_imitator_take(void *arguments) {
    struct bounded_buffer *b = arguments;
    take_buff(b);
    return NULL;
}
void test_take_buff_escapes_block_after_full() {
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);
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
 * Find Binary Header
 */
void test_find_binary_header_handles_random_data() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
    int vna_id = 0;
    char msg_buff[100];
    int start = 50000000;
    int step = 1000;
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, start+(step*(PPS-1)), PPS, MASK);
    
    write_command(vna_id,"info\r");
    sleep(1);
    write_command(vna_id,msg_buff);
    sleep(1);

    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(vna_id,&fp,MASK,PPS);
    TEST_ASSERT_EQUAL_INT(0,error);

    uint32_t freq;
    int bytes_read = read_exact(vna_id,(uint8_t*)&freq,sizeof(uint8_t)*4);
    TEST_ASSERT_EQUAL_INT(4,bytes_read);
    TEST_ASSERT_EQUAL_INT(start+step,freq);
}
void test_find_binary_header_constructs_correct_first_point() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
    int vna_id = 0;
    char msg_buff[100];
    int start = 50000000;
    int step = 1000;
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, start+(step*(PPS-1)), PPS, MASK);
    
    write_command(vna_id,msg_buff);
    sleep(1);

    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(vna_id,&fp,MASK,PPS);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,error);

    TEST_ASSERT_EQUAL_INT(start,fp.frequency);
}
void test_find_binary_header_fails_gracefully() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
    int vna_id = 0;
    char msg_buff[100];
    int start = 50000000;
    int step = 1000;
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, start+(step*(PPS-1)), PPS, MASK);
    
    write_command(vna_id,msg_buff);
    sleep(1);

    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(vna_id,&fp,MASK,PPS);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,error);
    error = find_binary_header(vna_id,&fp,MASK,PPS);
    TEST_ASSERT_NOT_EQUAL_INT(EXIT_SUCCESS,error);
}

/**
 * Pull Scan
 */
void test_pull_scan_constructs_valid_data() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read_exact()");
    int vna_id = 0;
    int start = 50000000;
    
    struct datapoint_nanoVNA_H* data = pull_scan(vna_id,start,start+(PPS*100000),PPS);

    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_INT(0,data->vna_id);
    TEST_ASSERT_NOT_NULL(data->point);
    for (int i = 0; i < PPS; i++) {
        TEST_ASSERT_EQUAL_INT(start+(i*PPS*1000),data->point[i].frequency);
    }

    free(data->point);
    free(data);
}
void test_pull_scan_takes_correct_number_points_low() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read_exact()");
    int points_per_scan = 1;
    int vna_id = 0;
    int start = 50000000;
    
    struct datapoint_nanoVNA_H* data = pull_scan(vna_id,start,start+(points_per_scan*100000), points_per_scan);

    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_NOT_NULL(data->point);
    for (int i = 0; i < points_per_scan; i++) {
        TEST_ASSERT_EQUAL_INT(start+(i*points_per_scan*1000),data->point[i].frequency);
    }

    free(data->point);
    free(data);
    points_per_scan = 101;
}
void test_pull_scan_takes_correct_number_points_high() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read_exact()");
    int points_per_scan = 201;
    int vna_id = 0;
    int start = 50000000;
    
    struct datapoint_nanoVNA_H* data = pull_scan(vna_id,start,start+(points_per_scan*100000), points_per_scan);

    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_NOT_NULL(data->point);
    for (int i = 0; i < points_per_scan; i++) {
        TEST_ASSERT_EQUAL_INT(start+(i*points_per_scan*500),data->point[i].frequency);
    }

    free(data->point);
    free(data);
    points_per_scan = 101;
}
void test_pull_scan_nulls_malformed_data() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read_exact()");
    int vna_id = 0;
    int start = 50000000;
    write_command(vna_id,"malform\r");
    sleep(1);
    
    struct datapoint_nanoVNA_H* data = pull_scan(vna_id,start,start+(PPS*100000),PPS);
    TEST_ASSERT_NULL(data);
    sleep(2);
}

/**
 * Producers
 */
void test_scan_producer_takes_correct_points() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Requires mocking pull_scan()");
    int vna_id = 0;
    int start = 50000000;

    int scan_id = 0;
    scan_states = calloc(sizeof(int),1);
    scan_states[scan_id] = 1;
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);

    int scans = 2;
    int size = ((scans*PPS-1)*100000);
    int step = size / (scans*PPS-1);

    struct scan_producer_args args;
    args.scan_id = scan_id;
    args.vna_id = vna_id;
    args.nbr_scans = scans;
    args.start = start;
    args.stop = start+size;
    args.nbr_sweeps = 1; 
    args.bfr = b;
    scan_producer(&args);
    for (int scan = 0; scan < scans; scan++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(b->buffer[scan], "Producer failed to cappture scan data");
        for (int i = 0; i < PPS; i++) {
            int expected = start+((scan*PPS + i)*step);
            TEST_ASSERT_EQUAL_INT(expected,b->buffer[scan]->point[i].frequency);
        }
        free(b->buffer[scan]->point);
        free(b->buffer[scan]);
    }
    destroy_bounded_buffer(b);
}
void test_timed_sweep_producer_takes_correct_time() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Requires mocking pull_scan()");
    int vna_id = 0;
    int start = 50000000;

    int scan_id = 0;
    scan_states = calloc(sizeof(int),1);
    scan_states[scan_id] = 1;
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);

    int scans = 2;
    int size = ((scans*PPS-1)*100000);
    int time_to_scan = 2;

    struct scan_producer_args scan_args;
    scan_args.scan_id = scan_id;
    scan_args.vna_id = vna_id;
    scan_args.nbr_scans = scans;
    scan_args.start = start;
    scan_args.stop = start+size;
    scan_args.nbr_sweeps = time_to_scan; 
    scan_args.bfr = b;

    struct scan_timer_args time_args;
    time_args.time_to_wait = time_to_scan;
    time_args.scan_id = scan_id;

    struct timeval start_time, end_time;
    pthread_t thread;

    gettimeofday(&start_time, NULL);
    int error = pthread_create(&thread, NULL, &scan_timer, &time_args);
    if(error != 0){
        fprintf(stderr, "Error %i creating timer thread for test_producer_time_takes_correct_time(): %s\n", errno, strerror(errno));
        return;
    }
    sweep_producer(&scan_args);
    gettimeofday(&end_time,NULL);

    int time_expired = end_time.tv_sec-start_time.tv_sec;
    TEST_ASSERT_GREATER_OR_EQUAL_INT(time_to_scan,time_expired);
    TEST_ASSERT_LESS_OR_EQUAL_INT(time_to_scan+1,time_expired);

    for (int i = 0; i < N; i++) {
        if (b->buffer[i]) {
            free(b->buffer[i]->point);
            free(b->buffer[i]);
        }
    }
    destroy_bounded_buffer(b);
}

/**
 * Consumer
 */
void test_consumer_constructs_valid_output() {
    TEST_IGNORE_MESSAGE("Requires mocking printf()");
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);

    struct datapoint_nanoVNA_H *data = calloc(1,sizeof(struct datapoint_nanoVNA_H));
    struct timeval time;
    gettimeofday(&time, NULL);

    data->vna_id = 1;
    data->send_time = time;
    data->receive_time = time;
    for (int i = 0; i < PPS; i++) {
        data->point[i] = (struct nanovna_raw_datapoint) {0,{0,0},{0,0}};
    }

    b->buffer[b->out] = data;
    b->count = 1;

    b->complete=0;

    struct timeval program_start_time;
    gettimeofday(&program_start_time, NULL);

    struct scan_consumer_args args;
    args.bfr = b;
    args.touchstone_file = NULL;
    args.id_string = "";
    args.label = "";
    args.verbose = false;
    args.program_start_time = program_start_time;
    scan_consumer(&args);

    // CHECK OUTPUT CORRECT (I'll figure out how later)

    TEST_ASSERT_EQUAL_INT(0,b->count);
    destroy_bounded_buffer(b);
}

/**
 * Scan State Logic
 */

extern int ongoing_scans;

// initialise_scan_state
void test_initialise_scan_state() {
    #ifndef TESTSUITE
    TEST_IGNORE_MESSAGE("Needs fix for static");
    #else

    ongoing_scans = 13;

    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, initialise_scan_state());
    TEST_ASSERT_NOT_NULL(scan_states);
    TEST_ASSERT_NOT_NULL(scan_threads);
    TEST_ASSERT_EQUAL_INT(0, ongoing_scans);
    #endif
}
void test_initialise_scan_state_already_done() {
    #ifndef TESTSUITE
    TEST_IGNORE_MESSAGE("Needs fix for static");
    #else

    scan_states = malloc(sizeof(int));
    scan_threads = malloc(sizeof(pthread_t));

    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, initialise_scan_state());
    TEST_ASSERT_NOT_NULL(scan_states);
    TEST_ASSERT_NOT_NULL(scan_threads);
    TEST_ASSERT_EQUAL_INT(0, ongoing_scans);
    #endif
}

// initialise_scan
void test_initialise_scan() {
    #ifndef TESTSUITE
    TEST_IGNORE_MESSAGE("Needs fix for static");
    #else
    initialise_scan_state();

    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0,scan_id);
    TEST_ASSERT_EQUAL_INT(0,scan_states[scan_id]);
    TEST_ASSERT_EQUAL_INT(1,ongoing_scans);
    #endif
}
void test_initialise_scan_max_ongoing() {
    #ifndef TESTSUITE
    TEST_IGNORE_MESSAGE("Needs fix for static");
    #else
    initialise_scan_state();

    ongoing_scans = MAX_ONGOING_SCANS;

    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(-1,scan_id);
    TEST_ASSERT_EQUAL_INT(MAX_ONGOING_SCANS,ongoing_scans);
    #endif
}
void test_initialise_scan_one_free_spot() {
    #ifndef TESTSUITE
    TEST_IGNORE_MESSAGE("Needs fix for static");
    #else
    initialise_scan_state();

    int fake_status = 10;
    ongoing_scans = MAX_ONGOING_SCANS-1;
    for (int i = 0; i < MAX_ONGOING_SCANS-1; i++) {
        scan_states[i] = fake_status;
    }

    int scan_id = initialise_scan();
    TEST_ASSERT_EQUAL_INT(MAX_ONGOING_SCANS-1,scan_id);
    TEST_ASSERT_EQUAL_INT(0,scan_states[scan_id]);
    TEST_ASSERT_EQUAL_INT(fake_status,scan_states[0]);
    TEST_ASSERT_EQUAL_INT(MAX_ONGOING_SCANS,ongoing_scans);
    #endif
}
void test_initialise_scan_uninitialised_states() {
    #ifndef TESTSUITE
    TEST_IGNORE_MESSAGE("Needs fix for static");
    #else
    
    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0,scan_id);
    TEST_ASSERT_EQUAL_INT(0,scan_states[scan_id]);
    TEST_ASSERT_EQUAL_INT(1,ongoing_scans);
    #endif
}

// destroy scan
void test_destroy_scan() {
    #ifndef TESTSUITE
    TEST_IGNORE_MESSAGE("Needs fix for static");
    #else
    initialise_scan_state();

    int scan_id = 0;
    scan_states[scan_id] = 0;
    ongoing_scans = 1;

    destroy_scan(scan_id);
    TEST_ASSERT_EQUAL_INT(-1,scan_states[scan_id]);
    TEST_ASSERT_EQUAL_INT(0,ongoing_scans);
    #endif
}

// is_running
void test_is_running_false() {
    scan_states = calloc(sizeof(int),MAX_ONGOING_SCANS);
    ongoing_scans = MAX_ONGOING_SCANS-1;
    for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
        scan_states[i] = 0;
    }
    
    int scan_id = 1;
    scan_states[scan_id] = -1;
    TEST_ASSERT_FALSE(is_running(scan_id));
}
void test_is_running_true() {
    scan_states = calloc(sizeof(int),MAX_ONGOING_SCANS);
    ongoing_scans = 1;
    for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
        scan_states[i] = -1;
    }
    
    int scan_id = 1;
    scan_states[scan_id] = 10;
    TEST_ASSERT_TRUE(is_running(scan_id));
}
void test_is_running_null() {
    TEST_ASSERT_FALSE(is_running(1));
}
void test_is_running_out_of_range() {
    scan_states = calloc(sizeof(int),MAX_ONGOING_SCANS);
    ongoing_scans = 0;

    TEST_ASSERT_FALSE(is_running(MAX_ONGOING_SCANS));
    TEST_ASSERT_FALSE(is_running(-1));
}

// get state
void test_get_state_vacant() {
    scan_states = calloc(sizeof(int),MAX_ONGOING_SCANS);
    ongoing_scans = 0;
    for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
        scan_states[i] = -1;
    }

    int scan_id = 1;

    char* state_buffer = calloc(sizeof(char),8);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, get_state(scan_id, state_buffer));
    TEST_ASSERT_EQUAL_STRING("vacant",state_buffer);

    free(state_buffer);
}
void test_get_state_idle() {
    scan_states = calloc(sizeof(int),MAX_ONGOING_SCANS);
    ongoing_scans = 0;
    for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
        scan_states[i] = -1;
    }

    int scan_id = 1;
    scan_states[scan_id] = 0;

    char* state_buffer = calloc(sizeof(char),8);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, get_state(scan_id, state_buffer));
    TEST_ASSERT_EQUAL_STRING("idle",state_buffer);

    free(state_buffer);
}
void test_get_state_ongoing() {
    scan_states = calloc(sizeof(int),MAX_ONGOING_SCANS);
    ongoing_scans = 0;
    for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
        scan_states[i] = -1;
    }

    int scan_id = 1;
    scan_states[scan_id] = 10;

    char* state_buffer = calloc(sizeof(char),8);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, get_state(scan_id, state_buffer));
    TEST_ASSERT_EQUAL_STRING("busy",state_buffer);

    free(state_buffer);
}
void test_get_state_null() {
    char* state_buffer = calloc(sizeof(char),8);
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, get_state(0, state_buffer));

    free(state_buffer);
}
void test_get_state_out_of_range() {
    scan_states = calloc(sizeof(int),MAX_ONGOING_SCANS);
    ongoing_scans = 0;
    for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
        scan_states[i] = -1;
    }

    char* state_buffer = calloc(sizeof(char),8);
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, get_state(MAX_ONGOING_SCANS, state_buffer));
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, get_state(-1, state_buffer));

    free(state_buffer);
}

/**
 * Scan Logic
 */
void test_stop_sweep_stops() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking vnas");

    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    int nbr_vnas = get_connected_vnas(vna_list);

    int scan_id = start_sweep(nbr_vnas, vna_list,1,50000000,55000000,ONGOING,1,PPS,"TestRun",false);
    sleep(1);
    TEST_ASSERT_GREATER_OR_EQUAL(0,scan_states[scan_id]);
    TEST_ASSERT_EQUAL_INT(1,ongoing_scans);

    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, stop_sweep(scan_id));
    TEST_ASSERT_EQUAL_INT(-1,scan_states[scan_id]);
    TEST_ASSERT_EQUAL_INT(0,ongoing_scans);
}

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 1) {
        // args for if using python simulator or not
        // if not, flag to skip serial tests
        vnas_mocked = argc - 1;
        mock_ports = (char **)&argv[1];
    }

    // bounded buffer tests
    RUN_TEST(test_create_bounded_buffer);
    RUN_TEST(test_add_buff_adds);
    RUN_TEST(test_add_buff_cycles);
    RUN_TEST(test_add_buff_escapes_block_after_full);
    RUN_TEST(test_take_buff_takes);
    RUN_TEST(test_take_buff_cycles);
    RUN_TEST(test_take_buff_escapes_block_after_full);

    // pull tests
    RUN_TEST(test_find_binary_header_handles_random_data);
    RUN_TEST(test_find_binary_header_constructs_correct_first_point);
    RUN_TEST(test_find_binary_header_fails_gracefully);
    RUN_TEST(test_pull_scan_constructs_valid_data);
    RUN_TEST(test_pull_scan_takes_correct_number_points_low);
    RUN_TEST(test_pull_scan_takes_correct_number_points_high);
    RUN_TEST(test_pull_scan_nulls_malformed_data);

    // producer/consumer tests
    RUN_TEST(test_scan_producer_takes_correct_points);
    RUN_TEST(test_timed_sweep_producer_takes_correct_time);
    RUN_TEST(test_consumer_constructs_valid_output);

    // scan state tests (private)
    RUN_TEST(test_initialise_scan_state);
    RUN_TEST(test_initialise_scan_state_already_done);
    RUN_TEST(test_initialise_scan);
    RUN_TEST(test_initialise_scan_max_ongoing);
    RUN_TEST(test_initialise_scan_one_free_spot);
    RUN_TEST(test_initialise_scan_uninitialised_states);
    RUN_TEST(test_destroy_scan);

    // scan state tests (public)
    RUN_TEST(test_is_running_false);
    RUN_TEST(test_is_running_true);
    RUN_TEST(test_is_running_null);
    RUN_TEST(test_is_running_out_of_range);

    RUN_TEST(test_get_state_vacant);
    RUN_TEST(test_get_state_idle);
    RUN_TEST(test_get_state_ongoing);
    RUN_TEST(test_get_state_null);
    RUN_TEST(test_get_state_out_of_range);

    // scan logic tests
    RUN_TEST(test_stop_sweep_stops);

    return UNITY_END();
}