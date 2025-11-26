#include "VnaScanMultithreaded.h"
#include <libserialport.h>
#include <string.h>

/*
 * Global variables
 */
struct sp_port **SERIAL_PORTS = NULL;
int VNA_COUNT = 0;

static volatile sig_atomic_t fatal_error_in_progress = 0;
struct timeval program_start_time;

/*
 * Fatal error / SIGINT handler
 */
void fatal_error_signal(int sig) {
    if (fatal_error_in_progress) {
        raise(sig);
    }
    fatal_error_in_progress = 1;

    close_and_reset_all();

    free(SERIAL_PORTS);
    SERIAL_PORTS = NULL;

    signal(sig, SIG_DFL);
    raise(sig);
}

/*
 * Open serial port using libserialport
 */
struct sp_port* open_serial(const char *port_name) {
    struct sp_port *port;

    if (sp_get_port_by_name(port_name, &port) != SP_OK) {
        fprintf(stderr, "Error: could not find serial port %s\n", port_name);
        return NULL;
    }

    if (sp_open(port, SP_MODE_READ_WRITE) != SP_OK) {
        fprintf(stderr, "Error: failed to open serial port %s\n", port_name);
        sp_free_port(port);
        return NULL;
    }

    return port;
}

/*
 * Configure serial port for NanoVNA
 */
void configure_serial(struct sp_port *port) {
    sp_set_baudrate(port, 115200);
    sp_set_bits(port, 8);
    sp_set_parity(port, SP_PARITY_NONE);
    sp_set_stopbits(port, 1);
    sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE);

    #ifdef __linux__
    sp_set_read_timeout(port, 1000);
    sp_set_write_timeout(port, 1000);
    #endif


}

/*
 * Close and cleanup serial ports
 */
void close_and_reset_all() {
    for (int i = VNA_COUNT - 1; i >= 0; i--) {
        sp_close(SERIAL_PORTS[i]);
        sp_free_port(SERIAL_PORTS[i]);
        VNA_COUNT--;
    }
}

/*
 * Write command using libserialport
 */
ssize_t write_command(struct sp_port *port, const char *cmd) {
    int len = strlen(cmd);
    int n = sp_blocking_write(port, cmd, len, 1000);

    if (n < 0) {
        fprintf(stderr, "Error writing to serial: %s\n", sp_last_error_message());
        return -1;
    }
    return n;
}

/*
 * Read an exact number of bytes
 */
ssize_t read_exact(struct sp_port *port, uint8_t *buffer, size_t length) {
    size_t total = 0;

    while (total < length) {
        int n = sp_blocking_read(port, buffer + total, length - total, 1000);

        if (n < 0) {
            fprintf(stderr, "Serial read error: %s\n", sp_last_error_message());
            return -1;
        }
        if (n == 0) {
            /* timeout */
            return total;
        }

        total += n;
    }

    return total;
}

/*
 * Find binary header
 */
int find_binary_header(struct sp_port *port, uint16_t expected_mask, uint16_t expected_points) {
    uint8_t window[4] = {0};
    int max_bytes = 500;

    if (read_exact(port, window, 4) != 4) {
        fprintf(stderr, "Failed to read initial header bytes\n");
        return -1;
    }

    uint16_t mask = window[0] | (window[1] << 8);
    uint16_t points = window[2] | (window[3] << 8);

    if (mask == expected_mask && points == expected_points) {
        return 1;
    }

    for (int i = 4; i < max_bytes; i++) {
        uint8_t byte;
        int n = sp_blocking_read(port, &byte, 1, 1000);

        if (n < 0) {
            fprintf(stderr, "Error reading header: %s\n", sp_last_error_message());
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "Timeout waiting for header\n");
            return 0;
        }

        window[0] = window[1];
        window[1] = window[2];
        window[2] = window[3];
        window[3] = byte;

        mask = window[0] | (window[1] << 8);
        points = window[2] | (window[3] << 8);

        if (mask == expected_mask && points == expected_points) {
            return 1;
        }
    }

    fprintf(stderr, "Binary header not found\n");
    return 0;
}

/*
 * Producer thread
 */
void* scan_producer(void *arguments) {
    struct scan_producer_args *args = (struct scan_producer_args*) arguments;

    struct timeval send_time, receive_time;

    for (int sweep = 0; sweep < args->nbr_sweeps; sweep++) {
        if (args->nbr_sweeps > 1) {
            printf("[Producer] Starting sweep %d/%d\n", sweep + 1, args->nbr_sweeps);
        }

        int total_scans = args->nbr_scans;
        int step = (args->stop - args->start) / total_scans;
        int current = args->start;

        while (total_scans > 0) {
            gettimeofday(&send_time, NULL);

            char msg_buf[50];
            snprintf(msg_buf, sizeof(msg_buf), "scan %d %d %i %i\r",
                current, (int)round(current + step), POINTS, MASK);

            if (write_command(args->serial_port, msg_buf) < 0) {
                fprintf(stderr, "Failed to send scan command\n");
                return NULL;
            }

            int header_found = find_binary_header(args->serial_port, MASK, POINTS);
            if (header_found != 1) {
                fprintf(stderr, "Binary header not found\n");
                return NULL;
            }

            struct datapoint_NanoVNAH *data = malloc(sizeof(struct datapoint_NanoVNAH));
            if (!data) {
                fprintf(stderr, "Allocation failure\n");
                return NULL;
            }

            for (int i = 0; i < POINTS; i++) {
                ssize_t br = read_exact(args->serial_port,
                    (uint8_t*)&data->point[i],
                    sizeof(struct nanovna_raw_datapoint));

                if (br != sizeof(struct nanovna_raw_datapoint)) {
                    fprintf(stderr, "Data point read error\n");
                    free(data);
                    return NULL;
                }
            }

            data->vna_id = args->vna_id;
            gettimeofday(&receive_time, NULL);
            data->send_time = send_time;
            data->recieve_time = receive_time;

            pthread_mutex_lock(&args->mtr->lock);
            while (args->mtr->count == N) {
                pthread_cond_wait(&args->mtr->take_cond, &args->mtr->lock);
            }

            args->mtr->buffer[args->mtr->in] = data;
            args->mtr->in = (args->mtr->in + 1) % N;
            args->mtr->count++;

            pthread_cond_signal(&args->mtr->add_cond);
            pthread_mutex_unlock(&args->mtr->lock);

            total_scans--;
            current += step;
        }
    }

    pthread_mutex_lock(&args->mtr->lock);
    args->mtr->complete++;
    pthread_cond_broadcast(&args->mtr->add_cond);
    pthread_mutex_unlock(&args->mtr->lock);

    return NULL;
}

/*
 * Consumer thread
 */
void* scan_consumer(void *arguments) {
    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;

    int total_count = 0;

    while (args->mtr->complete < VNA_COUNT || args->mtr->count > 0) {
        pthread_mutex_lock(&args->mtr->lock);

        while (args->mtr->count == 0 && args->mtr->complete < VNA_COUNT) {
            pthread_cond_wait(&args->mtr->add_cond, &args->mtr->lock);
        }

        if (args->mtr->count == 0) {
            pthread_mutex_unlock(&args->mtr->lock);
            break;
        }

        struct datapoint_NanoVNAH *data = args->mtr->buffer[args->mtr->out];
        args->mtr->buffer[args->mtr->out] = NULL;

        args->mtr->out = (args->mtr->out + 1) % N;
        args->mtr->count--;

        pthread_cond_signal(&args->mtr->take_cond);
        pthread_mutex_unlock(&args->mtr->lock);

        for (int i = 0; i < POINTS; i++) {
            printf("VNA%d (%d) s:%lf r:%lf | %u Hz: S11=%f+%fj, S21=%f+%fj\n",
                data->vna_id, total_count,
                ((double)(data->send_time.tv_sec - program_start_time.tv_sec)
                + (double)(data->send_time.tv_usec - program_start_time.tv_usec) / 1e6),
                ((double)(data->recieve_time.tv_sec - program_start_time.tv_sec)
                + (double)(data->recieve_time.tv_usec - program_start_time.tv_usec) / 1e6),
                data->point[i].frequency,
                data->point[i].s11.re, data->point[i].s11.im,
                data->point[i].s21.re, data->point[i].s21.im);

            total_count++;
        }

        free(data);
    }

    return NULL;
}

/*
 * Main scan controller
 */
void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, int nbr_sweeps, const char **ports) {
    VNA_COUNT = 0;

    SERIAL_PORTS = calloc(num_vnas, sizeof(struct sp_port*));
    if (!SERIAL_PORTS) {
        fprintf(stderr, "Failed to allocate SERIAL_PORTS\n");
        return;
    }

    gettimeofday(&program_start_time, NULL);

    struct datapoint_NanoVNAH **buffer =
        malloc(sizeof(struct datapoint_NanoVNAH*) * (N + 1));

    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        free(SERIAL_PORTS);
        SERIAL_PORTS = NULL;
        return;
    }

    struct buffer_monitor monitor = {
        .buffer = buffer,
        .in = 0,
        .out = 0,
        .count = 0,
        .complete = 0,
        .add_cond = PTHREAD_COND_INITIALIZER,
        .take_cond = PTHREAD_COND_INITIALIZER,
        .lock = PTHREAD_MUTEX_INITIALIZER
    };

    int error;
    struct scan_producer_args producer_args[num_vnas];
    pthread_t producers[num_vnas];

    for (int i = 0; i < num_vnas; i++) {
        SERIAL_PORTS[i] = open_serial(ports[i]);
        if (!SERIAL_PORTS[i]) {
            fprintf(stderr, "Failed to open port for VNA %d\n", i);

            for (int j = 0; j < i; j++) {
                sp_close(SERIAL_PORTS[j]);
                sp_free_port(SERIAL_PORTS[j]);
            }

            free(buffer);
            free(SERIAL_PORTS);
            SERIAL_PORTS = NULL;
            return;
        }

        configure_serial(SERIAL_PORTS[i]);
        VNA_COUNT++;

        producer_args[i].vna_id = i;
        producer_args[i].serial_port = SERIAL_PORTS[i];
        producer_args[i].nbr_scans = nbr_scans;
        producer_args[i].start = start;
        producer_args[i].stop = stop;
        producer_args[i].nbr_sweeps = nbr_sweeps;
        producer_args[i].mtr = &monitor;

        error = pthread_create(&producers[i], NULL, scan_producer, &producer_args[i]);
        if (error != 0) {
            fprintf(stderr, "Error creating producer thread %d\n", i);
            return;
        }
    }

    pthread_t consumer;
    struct scan_consumer_args consumer_args = { .mtr = &monitor };

    error = pthread_create(&consumer, NULL, scan_consumer, &consumer_args);
    if (error != 0) {
        fprintf(stderr, "Error creating consumer thread\n");
        return;
    }

    for (int i = 0; i < num_vnas; i++) {
        pthread_join(producers[i], NULL);
    }

    pthread_join(consumer, NULL);

    free(buffer);

    close_and_reset_all();
    free(SERIAL_PORTS);
    SERIAL_PORTS = NULL;
}

/*
 * Simple connection test using libserialport
 */
int test_connection(struct sp_port *port) {
    char buffer[64];

    if (write_command(port, "info\r") < 0) {
        fprintf(stderr, "Failed to send info command\n");
        return 1;
    }

    int n;
    do {
        n = sp_blocking_read(port, buffer, 63, 1000);

        if (n < 0) {
            fprintf(stderr, "Read error: %s\n", sp_last_error_message());
            return 1;
        }

        buffer[n] = '\0';
        printf("%s", buffer);

    } while (n > 0 && !strstr(buffer, "ch>"));

    return 0;
}
