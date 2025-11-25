#include "VnaScanMultithreaded.h"

/**
 * Declaring global variables (for error handling and port consistency)
 * 
 * @SERIAL_PORTS is a pointer to an array of all serial port connections
 * @INITIAL_PORT_SETTINGS is a pointer to an array of all serial port's initial settings
 * @VNA_COUNT is the number of VNAs currently connected
 */
struct sp_port **SERIAL_PORTS = NULL;
struct sp_port_config **INITIAL_PORT_SETTINGS = NULL;
int VNA_COUNT = 0;

static volatile sig_atomic_t fatal_error_in_progress = 0; // For proper SIGINT handling
volatile atomic_int complete = 0;
struct timeval program_start_time;

/**
 * Cross-platform timing function
 */
void get_current_time(struct timeval *tv) {
#ifdef _WIN32
    // MinGW64 provides gettimeofday, but we can use _ftime as fallback if needed
    #if defined(__MINGW64__) || defined(__MINGW32__)
        gettimeofday(tv, NULL);
    #else
        // Fallback for older Windows compilers
        struct _timeb timebuffer;
        _ftime(&timebuffer);
        tv->tv_sec = (long)timebuffer.time;
        tv->tv_usec = timebuffer.millitm * 1000;
    #endif
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
    
    if (INITIAL_PORT_SETTINGS) {
        for (int i = 0; i < VNA_COUNT; i++) {
            if (INITIAL_PORT_SETTINGS[i]) {
                sp_free_config(INITIAL_PORT_SETTINGS[i]);
            }
        }
        free(INITIAL_PORT_SETTINGS);
        INITIAL_PORT_SETTINGS = NULL;
    }
    
    if (SERIAL_PORTS) {
        free(SERIAL_PORTS);
        SERIAL_PORTS = NULL;
    }

    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Opens a serial port using libserialport
 * 
 * @param port_name The device path (e.g., "/dev/ttyACM0" on Linux, "COM3" on Windows)
 * @param port Output parameter for the opened port
 * @return 0 on success, -1 on failure
 */
int open_serial(const char *port_name, struct sp_port **port) {
    enum sp_return result;
    
    // Get port by name
    result = sp_get_port_by_name(port_name, port);
    if (result != SP_OK) {
        fprintf(stderr, "Error finding port %s: %s\n", port_name, sp_last_error_message());
        return -1;
    }
    
    // Open the port
    result = sp_open(*port, SP_MODE_READ_WRITE);
    if (result != SP_OK) {
        fprintf(stderr, "Error opening port %s: %s\n", port_name, sp_last_error_message());
        sp_free_port(*port);
        return -1;
    }
    
    return 0;
}

/**
 * Configures serial port settings for NanoVNA communication
 * 
 * Sets up 115200 baud, 8N1, raw mode, no flow control
 * Saves original settings for later restoration
 * 
 * @param port The serial port to configure
 * @return The original port configuration to restore later, NULL on error
 */
struct sp_port_config* configure_serial(struct sp_port *port) {
    enum sp_return result;
    struct sp_port_config *original_config;
    
    // Allocate config structure for saving original settings
    result = sp_new_config(&original_config);
    if (result != SP_OK) {
        fprintf(stderr, "Error allocating config: %s\n", sp_last_error_message());
        return NULL;
    }
    
    // Get current configuration
    result = sp_get_config(port, original_config);
    if (result != SP_OK) {
        fprintf(stderr, "Error getting port config: %s\n", sp_last_error_message());
        sp_free_config(original_config);
        return NULL;
    }
    
    // Configure port settings
    sp_set_baudrate(port, 115200);
    sp_set_bits(port, 8);
    sp_set_parity(port, SP_PARITY_NONE);
    sp_set_stopbits(port, 1);
    sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE);
    
    return original_config;
}

/**
 * Restores serial port to original settings
 * 
 * @param port The serial port
 * @param settings The original port configuration to restore
 */
void restore_serial(struct sp_port *port, struct sp_port_config *settings) {
    if (settings) {
        enum sp_return result = sp_set_config(port, settings);
        if (result != SP_OK) {
            fprintf(stderr, "Error restoring port settings: %s\n", sp_last_error_message());
        }
    }
}

/**
 * Closes all serial ports and restores their initial settings
 * Loops through ports in reverse order to maintain VNA_COUNT accuracy
 */
void close_and_reset_all() {
    for (int i = VNA_COUNT - 1; i >= 0; i--) {
        if (SERIAL_PORTS[i]) {
            // Restore original settings before closing
            if (INITIAL_PORT_SETTINGS && INITIAL_PORT_SETTINGS[i]) {
                restore_serial(SERIAL_PORTS[i], INITIAL_PORT_SETTINGS[i]);
            }
            
            // Close the serial port
            enum sp_return result = sp_close(SERIAL_PORTS[i]);
            if (result != SP_OK) {
                fprintf(stderr, "Error closing port %d: %s\n", i, sp_last_error_message());
            }
            
            // Free the port structure
            sp_free_port(SERIAL_PORTS[i]);
            SERIAL_PORTS[i] = NULL;
        }
        
        VNA_COUNT--;
    }
}

/**
 * Writes a command to the serial port with error checking
 * 
 * @param port The serial port
 * @param cmd The command string to send (should include \r terminator)
 * @return Number of bytes written on success, -1 on error
 */
ssize_t write_command(struct sp_port *port, const char *cmd) {
    size_t cmd_len = strlen(cmd);
    enum sp_return result = sp_blocking_write(port, cmd, cmd_len, SERIAL_WRITE_TIMEOUT);
    
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
 * With retry logic for better reliability on high-latency systems
 * 
 * @param port The serial port
 * @param buffer The buffer to read data into
 * @param length The number of bytes to read
 * @return Number of bytes read on success, -1 on error, 0 on timeout
 */
ssize_t read_exact(struct sp_port *port, uint8_t *buffer, size_t length) {
    size_t bytes_read = 0;
    int retry_count = 0;
    const int MAX_RETRIES = 3;
    
    while (bytes_read < length && retry_count < MAX_RETRIES) {
        enum sp_return result = sp_blocking_read(port, buffer + bytes_read, 
                                                  length - bytes_read, SERIAL_READ_TIMEOUT);
        
        if (result < 0) {
            fprintf(stderr, "Error reading from port: %s\n", sp_last_error_message());
            return -1;
        } else if (result == 0) {
            // Timeout - retry if we've made partial progress
            retry_count++;
            if (bytes_read > 0 && retry_count < MAX_RETRIES) {
                fprintf(stderr, "Timeout: read %zu of %zu bytes (retry %d/%d)\n", 
                        bytes_read, length, retry_count, MAX_RETRIES);
                // Continue trying - data might be coming slowly
                continue;
            } else {
                fprintf(stderr, "Timeout: only read %zu of %zu bytes\n", bytes_read, length);
                return bytes_read;
            }
        }
        
        bytes_read += result;
        
        // If we made progress, reset retry count
        if (result > 0) {
            retry_count = 0;
        }
    }
    
    if (bytes_read < length) {
        fprintf(stderr, "Failed to read complete data after %d retries: %zu of %zu bytes\n",
                MAX_RETRIES, bytes_read, length);
    }
    
    return bytes_read;
}

/**
 * Finds the binary header in the serial stream
 * Scans for header pattern (mask + points) using chunk-based reading
 * for better performance on Windows
 * 
 * @param port The serial port
 * @param expected_mask The expected mask value (e.g., 135)
 * @param expected_points The expected points value (e.g., 101)
 * @return 1 if header found, 0 if timeout/not found, -1 on error
 */
int find_binary_header(struct sp_port *port, uint16_t expected_mask, uint16_t expected_points) {
    uint8_t window[4] = {0};
    int max_bytes = 500;  // Maximum bytes to scan before giving up
    
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
    
    // Scan through the stream using chunk-based reading (OPTIMIZED)
    // Read chunks instead of single bytes to reduce USB overhead
    #define CHUNK_SIZE 32
    uint8_t chunk[CHUNK_SIZE];
    int total_bytes_scanned = 4;  // Already read 4 bytes
    
    while (total_bytes_scanned < max_bytes) {
        // Read a chunk of data
        int bytes_to_read = (max_bytes - total_bytes_scanned < CHUNK_SIZE) ? 
                            (max_bytes - total_bytes_scanned) : CHUNK_SIZE;
        
        enum sp_return result = sp_blocking_read(port, chunk, bytes_to_read, SERIAL_HEADER_TIMEOUT);
        
        if (result < 0) {
            fprintf(stderr, "Error reading header chunk: %s\n", sp_last_error_message());
            return -1;
        } else if (result == 0) {
            fprintf(stderr, "Timeout waiting for binary header\n");
            return 0;
        }
        
        // Scan through the chunk looking for header pattern
        for (int i = 0; i < result; i++) {
            // Shift window
            window[0] = window[1];
            window[1] = window[2];
            window[2] = window[3];
            window[3] = chunk[i];
            
            // Check for header match
            mask = window[0] | (window[1] << 8);
            points = window[2] | (window[3] << 8);
            
            if (mask == expected_mask && points == expected_points) {
                return 1;  // Found header
            }
            
            total_bytes_scanned++;
        }
    }
    
    fprintf(stderr, "Binary header not found after %d bytes\n", max_bytes);
    return 0;
    #undef CHUNK_SIZE
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
            
            // CRITICAL: Give NanoVNA time to complete scan and prepare data
            // Some firmware versions need this delay for reliable operation
            #ifdef _WIN32
                Sleep(150);  // 150ms delay on Windows
            #else
                usleep(150000);  // 150ms delay on Linux
            #endif
            
            // Flush input buffer to clear any echo or stale data
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

            // Set VNA ID (software metadata)
            data->vna_id = args->vna_id;
            // Set Timestamps
            get_current_time(&recieve_time);
            data->send_time = send_time;
            data->recieve_time = recieve_time;

            // Add to buffer
            pthread_mutex_lock(&args->thread_args->lock);
            while (args->thread_args->count == N) {
                pthread_cond_wait(&args->thread_args->remove_cond, &args->thread_args->lock);
            }
            args->buffer[args->thread_args->in] = data;
            args->thread_args->in = (args->thread_args->in + 1) % N;
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
    pthread_cond_broadcast(&args->thread_args->fill_cond); // Use broadcast for multiple consumers
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
    // Reset VNA_COUNT for clean state on subsequent runs
    VNA_COUNT = 0;
    
    // Initialise global variables
    SERIAL_PORTS = calloc(num_vnas, sizeof(struct sp_port*));
    INITIAL_PORT_SETTINGS = calloc(num_vnas, sizeof(struct sp_port_config*));
    
    if (!SERIAL_PORTS || !INITIAL_PORT_SETTINGS) {
        fprintf(stderr, "Failed to allocate memory for serial port arrays\n");
        if (SERIAL_PORTS) {
            free(SERIAL_PORTS);
            SERIAL_PORTS = NULL;
        }
        if (INITIAL_PORT_SETTINGS) {
            free(INITIAL_PORT_SETTINGS);
            INITIAL_PORT_SETTINGS = NULL;
        }
        return;
    }

    get_current_time(&program_start_time);

    // Create consumer and producer threads
    struct datapoint_NanoVNAH **buffer = malloc(sizeof(struct datapoint_NanoVNAH*) * (N + 1));
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer memory\n");
        free(SERIAL_PORTS);
        SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS);
        INITIAL_PORT_SETTINGS = NULL;
        return;
    }
    
    struct coordination_args thread_args = {
        .count = 0,
        .in = 0,
        .out = 0,
        .remove_cond = PTHREAD_COND_INITIALIZER,
        .fill_cond = PTHREAD_COND_INITIALIZER,
        .lock = PTHREAD_MUTEX_INITIALIZER
    };
    complete = 0;

    int error;

    struct scan_producer_args arguments[num_vnas];
    pthread_t producers[num_vnas];
    
    for (int i = 0; i < num_vnas; i++) {
        // Open serial port with error checking
        if (open_serial(ports[i], &SERIAL_PORTS[i]) < 0) {
            fprintf(stderr, "Failed to open serial port for VNA %d\n", i);
            // Clean up already opened ports
            for (int j = 0; j < i; j++) {
                restore_serial(SERIAL_PORTS[j], INITIAL_PORT_SETTINGS[j]);
                sp_close(SERIAL_PORTS[j]);
                sp_free_port(SERIAL_PORTS[j]);
                if (INITIAL_PORT_SETTINGS[j]) {
                    sp_free_config(INITIAL_PORT_SETTINGS[j]);
                }
            }
            free(buffer);
            buffer = NULL;
            free(SERIAL_PORTS);
            SERIAL_PORTS = NULL;
            free(INITIAL_PORT_SETTINGS);
            INITIAL_PORT_SETTINGS = NULL;
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
        if (error != 0) {
            fprintf(stderr, "Error creating producer thread %d\n", i);
            return;
        }
    }

    pthread_t consumer;
    struct scan_consumer_args consumer_args = {buffer, &thread_args};
    error = pthread_create(&consumer, NULL, &scan_consumer, &consumer_args);
    if (error != 0) {
        fprintf(stderr, "Error creating consumer thread\n");
        free(buffer);
        buffer = NULL;
        free(SERIAL_PORTS);
        SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS);
        INITIAL_PORT_SETTINGS = NULL;
        return;
    }

    // Wait for threads to finish
    for (int i = 0; i < num_vnas; i++) {
        error = pthread_join(producers[i], NULL);
        if (error != 0) {
            printf("Error from join producer\n");
            return;
        }
    }

    error = pthread_join(consumer, NULL);
    if (error != 0) {
        printf("Error from join consumer\n");
        return;
    }

    // Finish up
    free(buffer);
    buffer = NULL;

    close_and_reset_all();
    
    // Free configuration structures
    if (INITIAL_PORT_SETTINGS) {
        for (int i = 0; i < num_vnas; i++) {
            if (INITIAL_PORT_SETTINGS[i]) {
                sp_free_config(INITIAL_PORT_SETTINGS[i]);
            }
        }
        free(INITIAL_PORT_SETTINGS);
        INITIAL_PORT_SETTINGS = NULL;
    }
    
    free(SERIAL_PORTS);
    SERIAL_PORTS = NULL;

    return;
}

/**
 * Helper function. Issues info command and prints output
 * 
 * @param port The serial port
 * @return 0 on success, 1 on error
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
        numBytes = sp_blocking_read(port, buffer, 31, SERIAL_READ_TIMEOUT);
        if (numBytes < 0) {
            printf("Error reading: %s\n", sp_last_error_message());
            return 1;
        }
        buffer[numBytes] = '\0';
        printf("%s", buffer);
    } while (numBytes > 0 && !strstr(buffer, "ch>"));

    return 0;
}