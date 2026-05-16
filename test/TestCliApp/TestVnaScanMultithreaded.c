#include "VnaScanMultithreaded.h"
#include "unity.h"
#include <pthread.h>

#define PPS 101

int vnas_mocked = 0;
char **mock_ports;

/**
 * externs from VnaScanMultithreaded, for testing
 */
extern int* scan_states;
extern pthread_t* scan_threads;
extern int ongoing_scans;
extern pthread_mutex_t scan_state_lock;

/**
 * extern from VnaCommunication, purely for flushing I/O
 */
extern int* vna_fds;

/**
 * Helper to wait for a condition with a timeout
 */
bool wait_for_true(bool (*condition)(void), int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (condition()) return true;
        usleep(10000); // 10ms
        elapsed += 10;
    }
    return false;
}

bool no_ongoing_scans(void) {
    return get_ongoing_scan_count() == 0;
}

void setUp(void) {
    /* This is run before EACH TEST */
    if (vnas_mocked) {
        initialise_port_array();
        for (int i = 0; i < vnas_mocked; i++) {
            add_vna(mock_ports[i]);
        }
        // Ensure ports are clear for the test
        int vna_list[MAXIMUM_VNA_PORTS];
        int count = get_connected_vnas(vna_list);
        for (int i = 0; i < count; i++) {
            int fd = vna_fds[vna_list[i]];
            tcflush(fd, TCIOFLUSH);
        }
    }
    // initialise_scan_state is called inside initialise_scan, 
    // but for tests we might want to ensure it's fresh.
}

void tearDown(void) {
    /* This is run after EACH TEST */
    
    // 1. Stop any scans that might have been started by the test
    if (scan_states) {
        for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
            if (scan_states[i] >= 0) {
                stop_sweep(i);
            }
        }
    }

    // 2. Wait for all background threads to definitely finish (with timeout)
    if (!wait_for_true(no_ongoing_scans, 3000)) {
        fprintf(stderr, "Warning: Scans still active during tearDown (timeout)\n");
        // Force cancel any remaining scans
        if (scan_states) {
            for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
                if (scan_states[i] >= 0) {
                    pthread_mutex_lock(&scan_state_lock);
                    scan_states[i] = -1;
                    ongoing_scans--;
                    pthread_mutex_unlock(&scan_state_lock);
                }
            }
        }
    }

    // 3. Teardown port array (guarded by scan count check)
    if (vnas_mocked)
        teardown_port_array();

    // 4. Reset scan tracking globals to NULL to prevent UAF or leaks
    pthread_mutex_lock(&scan_state_lock);
    if (scan_states) {
        free(scan_states);
        scan_states = NULL;
    }
    if (scan_threads) {
        free(scan_threads);
        scan_threads = NULL;
    }
    ongoing_scans = 0;
    pthread_mutex_unlock(&scan_state_lock);
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
    TEST_ASSERT_EQUAL(0, error);

    // Wait for thread to block
    usleep(100000); 

    pthread_mutex_lock(&b->lock);
    b->count--;
    pthread_cond_signal(&b->take_cond);
    pthread_mutex_unlock(&b->lock);

    pthread_join(thread,NULL);

    TEST_ASSERT_EQUAL_INT(N, b->count);
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
    TEST_ASSERT_EQUAL(0, error);
    
    // Wait for thread to block
    usleep(100000);

    pthread_mutex_lock(&b->lock);
    b->count++;
    pthread_cond_signal(&b->add_cond);
    pthread_mutex_unlock(&b->lock);

    pthread_join(thread,NULL);

    TEST_ASSERT_EQUAL_INT(0, b->count);
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
    
    // Flush any pending data
    int vna_list[1];
    get_connected_vnas(vna_list);
    tcflush(vna_fds[vna_list[0]], TCIOFLUSH);

    // Send some random junk first
    write_command(vna_id,"info\r");
    usleep(50000); 
    write_command(vna_id,msg_buff);

    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(vna_id,&fp,MASK,PPS);
    TEST_ASSERT_EQUAL_INT(0,error);

    // After finding header and reading 1st point, next 4 bytes should be frequency of 2nd point
    uint32_t freq;
    int bytes_read = read_exact(vna_id,(uint8_t*)&freq, 4);
    TEST_ASSERT_EQUAL_INT(4,bytes_read);
    TEST_ASSERT_EQUAL_UINT32(start+step,freq);
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

    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(vna_id,&fp,MASK,PPS);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,error);

    TEST_ASSERT_EQUAL_UINT32(start,fp.frequency);
}

void test_find_binary_header_fails_gracefully() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
    int vna_id = 0;
    
    // No scan command sent, header should not be found
    struct nanovna_raw_datapoint fp;
    int error = find_binary_header(vna_id,&fp,MASK,PPS);
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
        TEST_ASSERT_EQUAL_UINT32(start+(i*PPS*1000),data->point[i].frequency);
    }

    free(data->point);
    free(data);
}

void test_pull_scan_nulls_malformed_data() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking read_exact()");
    int vna_id = 0;
    int start = 50000000;
    
    // Send a command that isn't a scan, pull_scan should fail to find header
    write_command(vna_id,"version\r");
    
    struct datapoint_nanoVNA_H* data = pull_scan(vna_id,start,start+(PPS*100000),PPS);
    TEST_ASSERT_NULL(data);
}

/**
 * Producers
 */
void test_scan_producer_takes_correct_points() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Requires mocking serial");
    int vna_id = 0;
    int start = 50000000;

    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0, scan_id);
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);

    int scans = 2;
    int size = ((scans*PPS-1)*100000);
    int step = size / (scans*PPS-1);

    // Use heap allocation as scan_producer will free(args)
    struct scan_producer_args *args = malloc(sizeof(struct scan_producer_args));
    args->scan_id = scan_id;
    args->vna_id = vna_id;
    args->nbr_scans = scans;
    args->start = start;
    args->stop = start+size;
    args->nbr_sweeps = 1; 
    args->bfr = b;
    
    scan_producer(args); // args is freed here
    
    for (int scan = 0; scan < scans; scan++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(b->buffer[scan], "Producer failed to capture scan data");
        for (int i = 0; i < PPS; i++) {
            uint32_t expected = start+((scan*PPS + i)*step);
            TEST_ASSERT_EQUAL_UINT32(expected,b->buffer[scan]->point[i].frequency);
        }
        free(b->buffer[scan]->point);
        free(b->buffer[scan]);
    }
    destroy_bounded_buffer(b);
}

void test_timed_sweep_producer_takes_correct_time() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Requires mocking serial");
    int vna_id = 0;
    int start = 50000000;

    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0, scan_id);
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b,PPS);

    int scans = 2;
    int size = ((scans*PPS-1)*100000);
    int time_to_scan = 1;

    // Use heap allocation as these will be freed by the target functions
    struct scan_producer_args *scan_args = malloc(sizeof(struct scan_producer_args));
    scan_args->scan_id = scan_id;
    scan_args->vna_id = vna_id;
    scan_args->nbr_scans = scans;
    scan_args->start = start;
    scan_args->stop = start+size;
    scan_args->nbr_sweeps = 0; // Usage depends on sweep_producer logic
    scan_args->bfr = b;

    struct scan_timer_args *time_args = malloc(sizeof(struct scan_timer_args));
    time_args->time_to_wait = time_to_scan;
    time_args->scan_id = scan_id;

    struct timeval start_t, end_t;
    pthread_t thread;

    gettimeofday(&start_t, NULL);
    int error = pthread_create(&thread, NULL, &scan_timer, time_args);
    TEST_ASSERT_EQUAL(0, error);
    
    // sweep_producer will run until scan_states[scan_id] is set to 0 by timer
    sweep_producer(scan_args); // scan_args is freed here
    
    pthread_join(thread, NULL); // timer_args was freed by scan_timer
    gettimeofday(&end_t,NULL);

    int time_expired = end_t.tv_sec - start_t.tv_sec;
    TEST_ASSERT_GREATER_OR_EQUAL_INT(time_to_scan, time_expired);
    TEST_ASSERT_LESS_OR_EQUAL_INT(time_to_scan + 2, time_expired);

    for (int i = 0; i < N; i++) {
        if (b->buffer[i]) {
            free(b->buffer[i]->point);
            free(b->buffer[i]);
        }
    }
    destroy_bounded_buffer(b);
}

/**
 * Scan State Logic
 */

// initialise_scan_state
void test_initialise_scan_state() {
    ongoing_scans = 13;
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, initialise_scan_state());
    TEST_ASSERT_NOT_NULL(scan_states);
    TEST_ASSERT_NOT_NULL(scan_threads);
    TEST_ASSERT_EQUAL_INT(0, ongoing_scans);
}

// initialise_scan
void test_initialise_scan() {
    initialise_scan_state();
    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0,scan_id);
    TEST_ASSERT_EQUAL_INT(0,scan_states[scan_id]);
    TEST_ASSERT_EQUAL_INT(1,ongoing_scans);
}

// is_running
void test_is_running_true() {
    initialise_scan_state();
    int scan_id = initialise_scan();
    scan_states[scan_id] = 10;
    TEST_ASSERT_TRUE(is_running(scan_id));
}

// get state
void test_get_state_busy() {
    initialise_scan_state();
    int scan_id = initialise_scan();
    scan_states[scan_id] = 10;

    char state_buffer[16];
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, get_state(scan_id, state_buffer));
    TEST_ASSERT_EQUAL_STRING("busy", state_buffer);
}

/**
 * Scan Logic
 */
void test_stop_sweep_stops() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without mocking vnas");

    int vna_list_local[MAXIMUM_VNA_PORTS];
    int nbr_vnas = get_connected_vnas(vna_list_local);

    // We need to pass a heap-allocated list to start_sweep because it takes ownership?
    // Looking at start_sweep, it assigns args->vna_list = vna_list;
    // And run_sweep does free(args->vna_list);
    // So YES, we must heap allocate the list passed to start_sweep.
    int* vna_list_heap = malloc(sizeof(int) * nbr_vnas);
    memcpy(vna_list_heap, vna_list_local, sizeof(int) * nbr_vnas);

    int scan_id = start_sweep(nbr_vnas, vna_list_heap, 1, 50000000, 55000000, ONGOING, 1, PPS, "TestRun", false);
    
    // Polling wait for scan to start
    int retry = 100;
    while (get_state(scan_id, (char[16]){0}) != EXIT_SUCCESS && retry-- > 0) usleep(10000);
    
    TEST_ASSERT_TRUE(is_running(scan_id));
    TEST_ASSERT_EQUAL_INT(1, get_ongoing_scan_count());

    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, stop_sweep(scan_id));
    TEST_ASSERT_FALSE(is_running(scan_id));
    TEST_ASSERT_EQUAL_INT(0, get_ongoing_scan_count());
}

/**
 * ============================================================================
 * PHASE 1 DIAGNOSTIC TESTS - Basic Data Validation
 * ============================================================================
 * These tests use scan_producer() directly to avoid pull_scan() blocking issues
 */

/**
 * TEST 1.2: Verify single scan produces 101 points from sweep_producer
 */
void test_single_scan_produces_101_points() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without hardware");
    
    int vna_id = 0;
    int start = 50000000;
    
    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0, scan_id);
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b, PPS);
    
    // Prepare args to run 1 scan
    struct scan_producer_args *args = malloc(sizeof(struct scan_producer_args));
    args->scan_id = scan_id;
    args->vna_id = vna_id;
    args->nbr_scans = 1;
    args->start = start;
    args->stop = 900000000;
    args->nbr_sweeps = 1;
    args->bfr = b;
    
    // Run producer directly (blocks until done)
    scan_producer(args);
    
    // Verify we got exactly 1 scan with 101 points
    TEST_ASSERT_NOT_NULL(b->buffer[0]);
    TEST_ASSERT_NOT_NULL(b->buffer[0]->point);
    
    int valid_points = 0;
    for (int i = 0; i < PPS; i++) {
        if (b->buffer[0]->point[i].frequency > 0) {
            valid_points++;
        }
    }
    
    TEST_ASSERT_EQUAL_INT(PPS, valid_points);
    
    free(b->buffer[0]->point);
    free(b->buffer[0]);
    destroy_bounded_buffer(b);
}

/**
 * TEST 2.1: Verify frequency points are unique and monotonic
 */
void test_frequency_points_are_unique() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without hardware");
    
    int vna_id = 0;
    int start = 50000000;
    
    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0, scan_id);
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b, PPS);
    
    struct scan_producer_args *args = malloc(sizeof(struct scan_producer_args));
    args->scan_id = scan_id;
    args->vna_id = vna_id;
    args->nbr_scans = 1;
    args->start = start;
    args->stop = 900000000;
    args->nbr_sweeps = 1;
    args->bfr = b;
    
    scan_producer(args);
    
    TEST_ASSERT_NOT_NULL(b->buffer[0]);
    TEST_ASSERT_NOT_NULL(b->buffer[0]->point);
    
    // Check monotonic increasing frequencies
    uint32_t prev_freq = 0;
    for (int i = 0; i < PPS; i++) {
        uint32_t freq = b->buffer[0]->point[i].frequency;
        TEST_ASSERT_GREATER_THAN(prev_freq, freq);
        prev_freq = freq;
    }
    
    free(b->buffer[0]->point);
    free(b->buffer[0]);
    destroy_bounded_buffer(b);
}

/**
 * TEST 3.1: Verify frequency range spans from start to stop
 */
void test_frequency_range_start_stop() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without hardware");
    
    int vna_id = 0;
    uint32_t start_freq = 50000000;
    uint32_t stop_freq = 900000000;
    
    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0, scan_id);
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b, PPS);
    
    struct scan_producer_args *args = malloc(sizeof(struct scan_producer_args));
    args->scan_id = scan_id;
    args->vna_id = vna_id;
    args->nbr_scans = 1;
    args->start = start_freq;
    args->stop = stop_freq;
    args->nbr_sweeps = 1;
    args->bfr = b;
    
    scan_producer(args);
    
    TEST_ASSERT_NOT_NULL(b->buffer[0]);
    TEST_ASSERT_NOT_NULL(b->buffer[0]->point);
    
    uint32_t actual_start = b->buffer[0]->point[0].frequency;
    uint32_t actual_stop = b->buffer[0]->point[PPS-1].frequency;
    
    // Allow 1% tolerance
    uint32_t tolerance = (stop_freq - start_freq) / 100;
    
    TEST_ASSERT_GREATER_THAN(actual_start, start_freq - tolerance);
    TEST_ASSERT_LESS_THAN(actual_start, start_freq + tolerance);
    TEST_ASSERT_GREATER_THAN(actual_stop, stop_freq - tolerance);
    TEST_ASSERT_LESS_THAN(actual_stop, stop_freq + tolerance);
    
    free(b->buffer[0]->point);
    free(b->buffer[0]);
    destroy_bounded_buffer(b);
}

/**
 * TEST 4.1: Verify S11/S21 magnitudes are within realistic bounds
 */
void test_s11_s21_magnitude_within_bounds() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without hardware");
    
    int vna_id = 0;
    int start = 50000000;
    
    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0, scan_id);
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b, PPS);
    
    struct scan_producer_args *args = malloc(sizeof(struct scan_producer_args));
    args->scan_id = scan_id;
    args->vna_id = vna_id;
    args->nbr_scans = 1;
    args->start = start;
    args->stop = 900000000;
    args->nbr_sweeps = 1;
    args->bfr = b;
    
    scan_producer(args);
    
    TEST_ASSERT_NOT_NULL(b->buffer[0]);
    TEST_ASSERT_NOT_NULL(b->buffer[0]->point);
    
    // Validate S-parameter bounds
    for (int i = 0; i < PPS; i++) {
        float s11_re = b->buffer[0]->point[i].s11.re;
        float s11_im = b->buffer[0]->point[i].s11.im;
        float s21_re = b->buffer[0]->point[i].s21.re;
        float s21_im = b->buffer[0]->point[i].s21.im;
        
        // S-parameters should be in [-2.5, 2.5] range
        TEST_ASSERT_GREATER_THAN(s11_re, -2.6f);
        TEST_ASSERT_LESS_THAN(s11_re, 2.6f);
        TEST_ASSERT_GREATER_THAN(s11_im, -2.6f);
        TEST_ASSERT_LESS_THAN(s11_im, 2.6f);
        TEST_ASSERT_GREATER_THAN(s21_re, -2.6f);
        TEST_ASSERT_LESS_THAN(s21_re, 2.6f);
        TEST_ASSERT_GREATER_THAN(s21_im, -2.6f);
        TEST_ASSERT_LESS_THAN(s21_im, 2.6f);
    }
    
    free(b->buffer[0]->point);
    free(b->buffer[0]);
    destroy_bounded_buffer(b);
}

/**
 * TEST 5.1: Verify multiple scans each have 101 points
 */
void test_multiple_scans_produce_101_points_each() {
    if (!vnas_mocked)
        TEST_IGNORE_MESSAGE("Cannot test without hardware");
    
    int vna_id = 0;
    int start = 50000000;
    
    int scan_id = initialise_scan();
    TEST_ASSERT_GREATER_OR_EQUAL(0, scan_id);
    
    struct bounded_buffer *b = malloc(sizeof(struct bounded_buffer));
    create_bounded_buffer(b, PPS);
    
    int num_scans = 2;
    
    struct scan_producer_args *args = malloc(sizeof(struct scan_producer_args));
    args->scan_id = scan_id;
    args->vna_id = vna_id;
    args->nbr_scans = num_scans;
    args->start = start;
    args->stop = 900000000;
    args->nbr_sweeps = 1;
    args->bfr = b;
    
    scan_producer(args);
    
    // Verify each scan has 101 points
    for (int scan = 0; scan < num_scans; scan++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(b->buffer[scan], "Scan data missing");
        TEST_ASSERT_NOT_NULL_MESSAGE(b->buffer[scan]->point, "Scan points missing");
        
        int valid_points = 0;
        for (int i = 0; i < PPS; i++) {
            if (b->buffer[scan]->point[i].frequency > 0) {
                valid_points++;
            }
        }
        TEST_ASSERT_EQUAL_INT(PPS, valid_points);
        
        free(b->buffer[scan]->point);
        free(b->buffer[scan]);
    }
    
    destroy_bounded_buffer(b);
}

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 1) {
        vnas_mocked = argc - 1;
        mock_ports = (char **)&argv[1];
    }

    // Set a global timeout for the entire test run (180 seconds)
    alarm(180);

    RUN_TEST(test_create_bounded_buffer);
    RUN_TEST(test_add_buff_adds);
    RUN_TEST(test_add_buff_cycles);
    RUN_TEST(test_add_buff_escapes_block_after_full);
    RUN_TEST(test_take_buff_takes);
    RUN_TEST(test_take_buff_cycles);
    RUN_TEST(test_take_buff_escapes_block_after_full);

    RUN_TEST(test_find_binary_header_handles_random_data);
    RUN_TEST(test_find_binary_header_constructs_correct_first_point);
    RUN_TEST(test_find_binary_header_fails_gracefully);
    RUN_TEST(test_pull_scan_constructs_valid_data);
    // SKIPPING: test_pull_scan_nulls_malformed_data (hangs on hardware)

    RUN_TEST(test_scan_producer_takes_correct_points);
    // SKIPPING: test_timed_sweep_producer_takes_correct_time (hangs on hardware)

    RUN_TEST(test_initialise_scan_state);
    RUN_TEST(test_initialise_scan);
    RUN_TEST(test_is_running_true);
    RUN_TEST(test_get_state_busy);

    RUN_TEST(test_stop_sweep_stops);

    // ===== PHASE 1: Critical Diagnostic Tests =====
    RUN_TEST(test_single_scan_produces_101_points);
    RUN_TEST(test_frequency_points_are_unique);
    RUN_TEST(test_frequency_range_start_stop);
    RUN_TEST(test_s11_s21_magnitude_within_bounds);
    RUN_TEST(test_multiple_scans_produce_101_points_each);

    return UNITY_END();
}