#include "VnaScanMultithreaded.h"
#include "unity.h"

#define UNITY_INCLUDE_CONFIG_H

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
    TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
}

void test_restore_serial_settings_correct() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
}

void test_close_and_reset_all_targets() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking restore_serial");
}
void test_close_and_reset_all_VNA_COUNT() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking restore_serial");
}

void test_write_command() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
}

void test_read_exact_reads_one_byte() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking serial connection");
}

void test_find_binary_header_handles_command_prompt() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
}
void test_find_binary_header_handles_random_data() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
}
void test_find_binary_header_fails_gracefully() {
    TEST_IGNORE_MESSAGE("Cannot test without mocking read()");
}

/**
 * Bounded Buffer create/destroy
 */
void test_create_bounded_buffer() {
    BoundedBuffer b = create_bounded_buffer(5);
    TEST_ASSERT_NOT_NULL(b.buffer);
    destroy_bounded_buffer(&b);
}

void test_destroy_bounded_buffer() {
    BoundedBuffer b = create_bounded_buffer(5);
    destroy_bounded_buffer(&b);
    TEST_ASSERT_NULL(b.buffer);
}

/**
 * Bounded buffer add
 */
void test_add_buff_adds() {
    BoundedBuffer b = create_bounded_buffer(5);
    TEST_ASSERT_EQUAL_INT(0,b.in);
    TEST_ASSERT_EQUAL_INT(0,b.count);
    b.buffer[b.in] = NULL;
    struct datapoint_NanoVNAH *data = calloc(1,sizeof(struct datapoint_NanoVNAH));
    add_buff(&b,data);
    TEST_ASSERT_EQUAL_INT(1,b.in);
    TEST_ASSERT_EQUAL_INT(1,b.count);
    TEST_ASSERT_NOT_NULL(b.buffer[b.out]);
    free(data);
    destroy_bounded_buffer(&b);
}
void test_add_buff_cycles() {
    BoundedBuffer b = create_bounded_buffer(N);
    b.in = N-1;
    b.buffer[b.in] = NULL;
    struct datapoint_NanoVNAH *data = calloc(1,sizeof(struct datapoint_NanoVNAH));
    add_buff(&b,data);
    TEST_ASSERT_EQUAL_INT(0,b.in);
    TEST_ASSERT_NOT_NULL(b.buffer[N-1]);
    free(data);
    destroy_bounded_buffer(&b);
}
struct thread_imitator_add_args {
    BoundedBuffer *b;
    struct datapoint_NanoVNAH *data;
};
void* thread_imitator_add(void *arguments) {
    struct thread_imitator_add_args *args = arguments;
    add_buff(args->b,args->data);
    return NULL;
}
void test_add_buff_escapes_block_after_full() {
    BoundedBuffer b = create_bounded_buffer(5);
    b.count = 5;
    b.buffer[b.in] = NULL;

    struct datapoint_NanoVNAH *data = calloc(1,sizeof(struct datapoint_NanoVNAH));
    struct thread_imitator_add_args args = {&b,data};
    pthread_t thread;
    int error = pthread_create(&thread, NULL, &thread_imitator_add, &args);
    if(error != 0){
        fprintf(stderr, "Error %i creating thread for test_add_buff_escapes_block_after_full(): %s\n", errno, strerror(errno));
        return;
    }

    b.count = 4;
    pthread_cond_signal(&b.add_cond);

    pthread_join(thread,NULL);

    TEST_ASSERT_EQUAL_INT(1,b.in);
    TEST_ASSERT_EQUAL_INT(5,b.count);
    TEST_ASSERT_NOT_NULL(b.buffer[b.out]);
    free(data);
    destroy_bounded_buffer(&b);
}

/**
 * Bounded Buffer take
 */
void test_take_buff_takes() {
    BoundedBuffer b = create_bounded_buffer(5);
    struct datapoint_NanoVNAH *data = calloc(1,sizeof(struct datapoint_NanoVNAH));
    b.buffer[b.out] = data;
    b.count = 1;
    struct datapoint_NanoVNAH *dataOut = take_buff(&b);
    TEST_ASSERT_EQUAL_INT(1,b.out);
    TEST_ASSERT_EQUAL_INT(0,b.count);
    TEST_ASSERT_NOT_NULL(dataOut);
    free(data);
    destroy_bounded_buffer(&b);
}
void test_take_buff_cycles() {
    BoundedBuffer b = create_bounded_buffer(N);
    b.out = N-1;
    struct datapoint_NanoVNAH *data = calloc(1,sizeof(struct datapoint_NanoVNAH));
    struct datapoint_NanoVNAH *dataOut = take_buff(&b);
    TEST_ASSERT_EQUAL_INT(0,b.out);
    TEST_ASSERT_NOT_NULL(dataOut);
    free(data);
    destroy_bounded_buffer(&b);
}
void* thread_imitator_take(void *arguments) {
    struct BoundedBuffer *b = arguments;
    take_buff(b);
    return NULL;
}
void test_take_buff_escapes_block_after_full() {
    BoundedBuffer b = create_bounded_buffer(5);
    b.count = 0;

    struct datapoint_NanoVNAH *data = calloc(1,sizeof(struct datapoint_NanoVNAH));
    b.buffer[b.out] = data;

    pthread_t thread;
    int error = pthread_create(&thread, NULL, &thread_imitator_take, &b);
    if(error != 0){
        fprintf(stderr, "Error %i creating thread for test_take_buff_escapes_block_after_full(): %s\n", errno, strerror(errno));
        return;
    }

    b.count = 1;
    pthread_cond_signal(&b.take_cond);

    pthread_join(thread,NULL);

    TEST_ASSERT_EQUAL_INT(1,b.out);
    TEST_ASSERT_EQUAL_INT(0,b.count);
    free(data);
    destroy_bounded_buffer(&b);
}

/**
 * Producer & Helpers
 */


/**
 * Consumer & Helpers
 */

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    if (argc > 0) {

    }
    
    RUN_TEST(test_configure_serial_settings_correct);
    RUN_TEST(test_restore_serial_settings_correct);
    RUN_TEST(test_close_and_reset_all_targets);
    RUN_TEST(test_close_and_reset_all_VNA_COUNT);
    RUN_TEST(test_write_command);
    RUN_TEST(test_read_exact_reads_one_byte);
    RUN_TEST(test_find_binary_header_handles_command_prompt);
    RUN_TEST(test_find_binary_header_handles_random_data);
    RUN_TEST(test_find_binary_header_fails_gracefully);
    RUN_TEST(test_create_bounded_buffer);
    RUN_TEST(test_destroy_bounded_buffer);
    RUN_TEST(test_add_buff_adds);
    RUN_TEST(test_add_buff_cycles);
    RUN_TEST(test_add_buff_escapes_block_after_full);
    RUN_TEST(test_take_buff_takes);
    RUN_TEST(test_take_buff_cycles);
    RUN_TEST(test_take_buff_escapes_block_after_full);

    return UNITY_END();
}