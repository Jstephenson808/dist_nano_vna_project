#include "VnaScanMultithreaded.h"

/**
 * Declaring global variables (for error handling and port consistency)
 */
struct sp_port **SERIAL_PORTS = NULL;
struct sp_port_config **INITIAL_PORT_SETTINGS = NULL;
int VNA_COUNT = 0;

static volatile sig_atomic_t fatal_error_in_progress = 0;
volatile atomic_int complete = 0;
struct timeval program_start_time;

// Cross-platform timing function
void get_current_time(struct timeval *tv) {
#ifdef _WIN32
    struct _timeb timebuffer;
    _ftime(&timebuffer);
    tv->tv_sec = (long)timebuffer.time;
    tv->tv_usec = timebuffer.millitm * 1000;
#else
    gettimeofday(tv, NULL);
#endif
}

void fatal_error_signal(int sig) {
    if (fatal_error_in_progress) {
        raise(sig);
    }
    fatal_error_in_progress = 1;

    close_and_reset_all();
    free(INITIAL_PORT_SETTINGS);
    INITIAL_PORT_SETTINGS = NULL;
    free(SERIAL_PORTS);
    SERIAL_PORTS = NULL;

    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Opens a serial port
 */
struct sp_port* open_serial(const char *port) {
    struct sp_port *sp;
    
    if (sp_get_port_by_name(port, &sp) != SP_OK) {
        fprintf(stderr, "Error finding serial port %s\n", port);
        return NULL;
    }
    
    if (sp_open(sp, SP_MODE_READ_WRITE) != SP_OK) {
        fprintf(stderr, "Error opening serial port %s: %s\n", port, sp_last_error_message());
        sp_free_port(sp);
        return NULL;
    }
    
    return sp;
}

/**
 * Configures serial port settings for NanoVNA communication
 */
struct sp_port_config* configure_serial(struct sp_port *port) {
    // Save initial settings
    struct sp_port_config *initial_config;
    sp_new_config(&initial_config);
    sp_get_config(port, initial_config);
    
    // Configure port: 115200 baud, 8N1, no flow control
    sp_set_baudrate(port, 115200);
    sp_set_bits(port, 8);
    sp_set_parity(port, SP_PARITY_NONE);
    sp_set_stopbits(port, 1);
    sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE);
    
    return initial_config;
}

/**
 * Restores serial port to original settings
 */
void restore_serial(struct sp_port *port, struct sp_port_config *settings) {
    if (settings != NULL) {
        sp_set_config(port, settings);
        sp_free_config(settings);
    }
}

/**
 * Closes all serial ports and restores their initial settings
 */
void close_and_reset_all() {
    for (int i = VNA_COUNT-1; i >= 0; i--) {
        restore_serial(SERIAL_PORTS[i], INITIAL_PORT_SETTINGS[i]);
        sp_close(SERIAL_PORTS[i]);
        sp_free_port(SERIAL_PORTS[i]);
        VNA_COUNT--;
    }
}

/**
 * Writes a command to the serial port with error checking
 */
ssize_t write_command(struct sp_port *port, const char *cmd) {
    size_t cmd_len = strlen(cmd);
    enum sp_return result = sp_blocking_write(port, cmd, cmd_len, 1000);
    
    if (result < 0) {
        fprintf(stderr, "Error writing to port: %s\n", sp_last_error_message());
        return -1;
    } else if (result < (int)cmd_len) {
        fprintf(stderr, "Warning: Partial write (%d of %zu bytes)\n", result, cmd_len);
    }
    
    return result;
}

/**
 * Reads exact number of bytes from serial port
 * Handles partial reads by continuing until all bytes are received
 */
ssize_t read_exact(struct sp_port *port, uint8_t *buffer, size_t length) {
    size_t bytes_read = 0;
    
    while (bytes_read < length) {
        enum sp_return result = sp_blocking_read(port, buffer + bytes_read, 
                                                  length - bytes_read, 2000);
        
        if (result < 0) {
            fprintf(stderr, "Error reading from port: %s\n", sp_last_error_message());
            return -1;
        } else if (result == 0) {
            // Timeout
            if (bytes_read > 0) {
                fprintf(stderr, "Timeout: only read %zu of %zu bytes\n", bytes_read, length);
            }
            return bytes_read;
        }
        
        bytes_read += result;
    }
    
    return bytes_read;
}

/**
 * Finds the binary header in the serial stream
 * Scans byte-by-byte looking for the header pattern (mask + points)
 * 
 * ORIGINAL ALGORITHM - Minimal changes from Linux version
 */
int find_binary_header(struct sp_port *port, uint16_t expected_mask, uint16_t expected_points) {
    uint8_t window[4] = {0};
    int max_bytes = 500;
    
    // Read initial 4 bytes
    if (read_exact(port, window, 4) != 4) {
        fprintf(stderr, "Failed to read initial header bytes\n");
        return -1;
    }
    
    // Check if we already have the header
    uint16_t mask = window[0] | (window[1] << 8);
    uint16_t points = window[2] | (window[3] << 8);
    if (mask == expected_mask && points == expected_points) {
        return 1;
    }
    
    // Scan through the stream byte by byte (ORIGINAL APPROACH)
    for (int i = 4; i < max_bytes; i++) {
        uint8_t byte;
        enum sp_return result = sp_blocking_read(port, &byte, 1, 1000);
        
        if (result < 0) {
            fprintf(stderr, "Error reading header byte: %s\n", sp_last_error_message());
            return -1;
        } else if (result == 0) {
            fprintf(stderr, "Timeout waiting for binary header\n");
            return 0;
        }
        
        // Shift window
        window[0] = window[1];
        window[1] = window[2];
        window[2] = window[3];
        window[3] = byte;
        
        // Check for header match
        mask = window[0] | (window[1] << 8);
        points = window[2] | (window[3] << 8);
        
        if (mask == expected_mask && points == expected_points) {
            return 1;  // Found header
        }
    }
    
    fprintf(stderr, "Binary header not found after %d bytes\n", max_bytes);
    return 0;
}

void* scan_producer(void *arguments) {
    struct scan_producer_args *args = (struct scan_producer_args*)arguments;
    struct timeval send_time, recieve_time;

    for (int sweep = 0; sweep < args->nbr_sweeps; sweep++) {
        if (args->nbr_sweeps > 1) {
            printf("[Producer] Starting sweep %d/%d\n", sweep + 1, args->nbr_sweeps);
        }
        int total_scans = args->nbr_scans;
        int step = (args->stop - args->start) / total_scans;
        int current = args->start;
        
        while (total_scans > 0) {
            // Send scan command
            get_current_time(&send_time);
            char msg_buff[50];
            snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", 
                    current, (int)round(current + step), POINTS, MASK);
            
            if (write_command(args->serial_port, msg_buff) < 0) {
                fprintf(stderr, "Failed to send scan command\n");
                return NULL;
            }
            
            // Give device time to prepare (helps with reliability)
            #ifdef _WIN32
                Sleep(150);
            #else
                usleep(150000);
            #endif
            
            // Flush any echo or stale data
            sp_flush(args->serial_port, SP_BUF_INPUT);

            // Find binary header in response
            int header_found = find_binary_header(args->serial_port, MASK, POINTS);
            if (header_found != 1) {
                fprintf(stderr, "Failed to find binary header\n");
                return NULL;
            }

            // Receive data points
            struct datapoint_NanoVNAH *data = malloc(sizeof(struct datapoint_NanoVNAH));
            if (!data) {
                fprintf(stderr, "Failed to allocate memory for data points\n");
                return NULL;
            }

            for (int i = 0; i < POINTS; i++) {
                // Read raw data (20 bytes from NanoVNA)
                ssize_t bytes_read = read_exact(args->serial_port, 
                                                (uint8_t*)&data->point[i], 
                                                sizeof(struct nanovna_raw_datapoint));
                
                if (bytes_read != sizeof(struct nanovna_raw_datapoint)) {
                    fprintf(stderr, "Error reading data point %d: got %zd bytes, expected %zu\n", 
                            i, bytes_read, sizeof(struct nanovna_raw_datapoint));
                    free(data);
                    return NULL;
                }
            }

            // Set VNA ID and timestamps
            data->vna_id = args->vna_id;
            get_current_time(&recieve_time);
            data->send_time = send_time;
            data->recieve_time = recieve_time;

            // Add to buffer
            pthread_mutex_lock(&args->thread_args->lock);
            while (args->thread_args->count == N) {
                pthread_cond_wait(&args->thread_args->remove_cond, &args->thread_args->lock);
            }
            args->buffer[args->thread_args->in] = data;
            args->thread_args->in = (args->thread_args->in+1) % N;
            args->thread_args->count++;
            pthread_cond_signal(&args->thread_args->fill_cond);
            pthread_mutex_unlock(&args->thread_args->lock);

            // Finish loop
            total_scans--;
            current += step;
        }       
    }
    
    pthread_mutex_lock(&args->thread_args->lock);
    complete++;
    pthread_cond_broadcast(&args->thread_args->fill_cond);
    pthread_mutex_unlock(&args->thread_args->lock);

    return NULL;
}

void* scan_consumer(void *arguments) {
    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;
    int total_count = 0;

    while (complete < VNA_COUNT || (args->thread_args->count != 0)) {
        pthread_mutex_lock(&args->thread_args->lock);
        while (args->thread_args->count == 0 && complete < VNA_COUNT) {
            pthread_cond_wait(&args->thread_args->fill_cond, &args->thread_args->lock);
        }

        struct datapoint_NanoVNAH *data = args->buffer[args->thread_args->out];
        args->buffer[args->thread_args->out] = NULL;
        args->thread_args->out = (args->thread_args->out + 1) % N;
        args->thread_args->count--;
        pthread_cond_signal(&args->thread_args->remove_cond);
        pthread_mutex_unlock(&args->thread_args->lock);

        for (int i = 0; i < POINTS; i++) {
            printf("VNA%d (%d) s:%lf r:%lf | %u Hz: S11=%f+%fj, S21=%f+%fj\n", 
                   data->vna_id, total_count,
                   ((double)(data->send_time.tv_sec - program_start_time.tv_sec) + 
                    (double)(data->send_time.tv_usec - program_start_time.tv_usec) / 1000000.0),
                   ((double)(data->recieve_time.tv_sec - program_start_time.tv_sec) + 
                    (double)(data->recieve_time.tv_usec - program_start_time.tv_usec) / 1000000.0),
                   data->point[i].frequency, 
                   data->point[i].s11.re, data->point[i].s11.im, 
                   data->point[i].s21.re, data->point[i].s21.im);
            total_count++;
        }

        free(data);
        data = NULL;
    }
    return NULL;
}

void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, int nbr_sweeps, const char **ports) {
    // Reset VNA_COUNT for clean state
    VNA_COUNT = 0;
    
    // Initialise global variables
    SERIAL_PORTS = calloc(num_vnas, sizeof(struct sp_port*));
    INITIAL_PORT_SETTINGS = calloc(num_vnas, sizeof(struct sp_port_config*));
    
    if (!SERIAL_PORTS || !INITIAL_PORT_SETTINGS) {
        fprintf(stderr, "Failed to allocate memory for serial port arrays\n");
        if (SERIAL_PORTS) {free(SERIAL_PORTS); SERIAL_PORTS = NULL;}
        if (INITIAL_PORT_SETTINGS) {free(INITIAL_PORT_SETTINGS); INITIAL_PORT_SETTINGS = NULL;}
        return;
    }

    get_current_time(&program_start_time);

    // Create buffer
    struct datapoint_NanoVNAH **buffer = malloc(sizeof(struct datapoint_NanoVNAH*)*(N+1));
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer memory\n");
        free(SERIAL_PORTS); SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS); INITIAL_PORT_SETTINGS = NULL;
        return;
    }
    
    struct coordination_args thread_args = {0,0,0,PTHREAD_COND_INITIALIZER,PTHREAD_COND_INITIALIZER,PTHREAD_MUTEX_INITIALIZER};
    complete = 0;

    int error;

    // Create producer threads
    struct scan_producer_args arguments[num_vnas];
    pthread_t producers[num_vnas];
    
    for(int i = 0; i < num_vnas; i++) {
        // Open serial port
        SERIAL_PORTS[i] = open_serial(ports[i]);
        if (SERIAL_PORTS[i] == NULL) {
            fprintf(stderr, "Failed to open serial port for VNA %d\n", i);
            // Clean up already opened ports
            for (int j = 0; j < i; j++) {
                restore_serial(SERIAL_PORTS[j], INITIAL_PORT_SETTINGS[j]);
                sp_close(SERIAL_PORTS[j]);
                sp_free_port(SERIAL_PORTS[j]);
            }
            free(buffer); buffer = NULL;
            free(SERIAL_PORTS); SERIAL_PORTS = NULL;
            free(INITIAL_PORT_SETTINGS); INITIAL_PORT_SETTINGS = NULL;
            return;
        }
        
        // Configure serial port and save original settings
        INITIAL_PORT_SETTINGS[i] = configure_serial(SERIAL_PORTS[i]);
        VNA_COUNT++;

        arguments[i].vna_id = i;
        arguments[i].serial_port = SERIAL_PORTS[i];
        arguments[i].nbr_scans = nbr_scans;
        arguments[i].start = start;
        arguments[i].stop = stop;
        arguments[i].nbr_sweeps = nbr_sweeps;
        arguments[i].buffer = buffer;
        arguments[i].thread_args = &thread_args;

        error = pthread_create(&producers[i], NULL, &scan_producer, &arguments[i]);
        if(error != 0){
            fprintf(stderr, "Error creating producer thread %d\n", i);
            return;
        }
    }

    // Create consumer thread
    pthread_t consumer;
    struct scan_consumer_args consumer_args = {buffer, &thread_args};
    error = pthread_create(&consumer, NULL, &scan_consumer, &consumer_args);
    if(error != 0){
        fprintf(stderr, "Error creating consumer thread\n");
        free(buffer); buffer = NULL;
        free(SERIAL_PORTS); SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS); INITIAL_PORT_SETTINGS = NULL;
        return;
    }

    // Wait for threads to finish
    for(int i = 0; i < num_vnas; i++) {
        error = pthread_join(producers[i], NULL);
        if(error != 0){
            fprintf(stderr, "Error joining producer %d\n", i);
            return;
        }
    }

    error = pthread_join(consumer, NULL);
    if(error != 0){
        fprintf(stderr, "Error joining consumer\n");
        return;
    }

    // Cleanup
    free(buffer);
    buffer = NULL;

    close_and_reset_all();
    free(SERIAL_PORTS);
    SERIAL_PORTS = NULL;
    free(INITIAL_PORT_SETTINGS);
    INITIAL_PORT_SETTINGS = NULL;

    return;
}

/**
 * Helper function. Issues info command and prints output
 */
int test_connection(struct sp_port *port) {
    char buffer[32];
    const char *msg = "info\r";
    
    if (write_command(port, msg) < 0) {
        fprintf(stderr, "Failed to send info command\n");
        return 1;
    }

    int numBytes;
    do {
        numBytes = sp_blocking_read(port, buffer, 31, 1000);
        if (numBytes < 0) {
            fprintf(stderr, "Error reading: %s\n", sp_last_error_message());
            return 1;
        }
        buffer[numBytes] = '\0';
        printf("%s", buffer);
    } while (numBytes > 0 && !strstr(buffer, "ch>"));

    return 0;
}