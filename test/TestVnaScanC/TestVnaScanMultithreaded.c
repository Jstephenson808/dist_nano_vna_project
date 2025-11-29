#include "VnaScanMultithreaded.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

int vna_mocked = 0;

void setUp(void) {
    /* This is run before EACH TEST */
}

void tearDown(void) {
    /* This is run after EACH TEST */
}

/**
 * Serial functions
 */
void test_configure_serial_settings_correct() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
}

void test_restore_serial_settings_correct() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
}

void test_close_and_reset_all_targets() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking restore_serial");}
}
void test_close_and_reset_all_VNA_COUNT() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking restore_serial");}
}

void test_write_command() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
}

void test_read_exact_reads_one_byte() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");}
}

void test_find_binary_header_handles_command_prompt() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read()");}
}
void test_find_binary_header_handles_random_data() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read()");}
}
void test_find_binary_header_fails_gracefully() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read()");}
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
}
void test_pull_scan_nulls_malformed_data() {
    if (!vna_mocked) {TEST_IGNORE_MESSAGE("Cannot test without mocking read_exact()");}
}
void test_producer_takes_correct_points() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking pull_scan()");
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

    extern int VNA_COUNT;
    VNA_COUNT = 1;
    b->complete=1;

    struct scan_consumer_args args = {b};
    scan_consumer(&args);

    // CHECK OUTPUT CORRECT (I'll figure out how later)

    TEST_ASSERT_EQUAL_INT(0,b->count);
    free(data);
    destroy_bounded_buffer(b);
}

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 0) {
        // args for if using python simulator or not
        // if not, flag to skip serial tests
        // defaults tbd
    }
    
    // serial tests
    RUN_TEST(test_configure_serial_settings_correct);
    RUN_TEST(test_restore_serial_settings_correct);
    RUN_TEST(test_close_and_reset_all_targets);
    RUN_TEST(test_close_and_reset_all_VNA_COUNT);
    RUN_TEST(test_write_command);
    RUN_TEST(test_read_exact_reads_one_byte);
    RUN_TEST(test_find_binary_header_handles_command_prompt);
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