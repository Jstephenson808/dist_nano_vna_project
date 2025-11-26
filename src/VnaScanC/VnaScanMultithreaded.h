#ifndef VNA_SCAN_MULTITHREADED_H
#define VNA_SCAN_MULTITHREADED_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include <libserialport.h>
#include <signal.h>
#include <math.h>

#define POINTS 101
#define MASK   135
#define N      64     // buffer size

/*
 * Raw NanoVNA datapoint
 */
struct complex_val {
    float re;
    float im;
};

struct nanovna_raw_datapoint {
    uint32_t frequency;
    struct complex_val s11;
    struct complex_val s21;
};

/*
 * Buffer item: one whole sweep chunk
 */
struct datapoint_NanoVNAH {
    int vna_id;
    struct timeval send_time;
    struct timeval recieve_time;
    struct nanovna_raw_datapoint point[POINTS];
};

/*
 * Producer/consumer shared buffer monitor
 */
struct buffer_monitor {
    struct datapoint_NanoVNAH **buffer;
    int in;
    int out;
    int count;
    int complete;
    pthread_cond_t add_cond;
    pthread_cond_t take_cond;
    pthread_mutex_t lock;
};

/*
 * Producer thread arguments
 */
struct scan_producer_args {
    int vna_id;
    struct sp_port *serial_port;
    int nbr_scans;
    int start;
    int stop;
    int nbr_sweeps;
    struct buffer_monitor *mtr;
};

/*
 * Consumer thread arguments
 */
struct scan_consumer_args {
    struct buffer_monitor *mtr;
};

/*
 * Function prototypes
 */

void fatal_error_signal(int sig);

struct sp_port* open_serial(const char *port_name);

void configure_serial(struct sp_port *port);

void close_and_reset_all();

ssize_t write_command(struct sp_port *port, const char *cmd);

ssize_t read_exact(struct sp_port *port, uint8_t *buffer, size_t length);

int find_binary_header(struct sp_port *port, uint16_t expected_mask, uint16_t expected_points);

void* scan_producer(void *arguments);

void* scan_consumer(void *arguments);

void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, int nbr_sweeps, const char **ports);

int test_connection(struct sp_port *port);

#endif
