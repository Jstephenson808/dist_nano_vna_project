#include "VnaScanMultithreaded.h"

/**
 * Declaring global variables (for error handling and port consistency)
 * 
 * @SERIAL_PORTS is a pointer to an array of all serial port connections
 * @INITIAL_PORT_SETTINGS is a pointer to an array of all serial port's initial settings
 * @VNA_COUNT is the number of VNAs currently connected
 */
int *SERIAL_PORTS = NULL;
struct termios* INITIAL_PORT_SETTINGS = NULL;
int VNA_COUNT_GLOBAL = 0;

static volatile sig_atomic_t fatal_error_in_progress = 0; // For proper SIGINT handling
struct timeval program_start_time;

// Global state for async scanning (declared early for use in scan_timer)
static pthread_t async_scan_thread;
static volatile int async_scan_active = 0;
static volatile int async_thread_created = 0;  // Track if thread was ever created
static DataCallback async_data_callback = NULL;
static StatusCallback async_status_callback = NULL;
static ErrorCallback async_error_callback = NULL;

void fatal_error_signal(int sig) {
    if (fatal_error_in_progress) {
        raise (sig);
    }
    fatal_error_in_progress = 1;

    close_and_reset_all();
    free(INITIAL_PORT_SETTINGS);
    INITIAL_PORT_SETTINGS = NULL;
    free(SERIAL_PORTS);
    SERIAL_PORTS = NULL;

    signal (sig, SIG_DFL);
    raise (sig);
}

/**
 * Opens a serial port
 * 
 * @param port The device path (e.g., "/dev/ttyACM0")
 * @return File descriptor on success, -1 on failure
 */
int open_serial(const char *port) {
    int fd = open(port, O_RDWR | O_NOCTTY);
    
    if (fd < 0) {
        fprintf(stderr, "Error opening serial port %s: %s\n", port, strerror(errno));
        return -1;
    }
    return fd;
}

/**
 * Configures serial port settings for NanoVNA communication
 * 
 * Sets up 115200 baud, 8N1, raw mode, no flow control
 * Saves original settings for later restoration
 * 
 * @param serial_port The file descriptor of the open serial port
 * @return The original termios settings to restore later
 */
int configure_serial(int serial_port, struct termios *initial_tty) {
    int error = tcgetattr(serial_port, initial_tty); // put actual initial tty in
    if (error != 0) {
        fprintf(stderr, "Error %i from tcgetattr: %s\n", errno, strerror(errno));
        return EXIT_FAILURE;
    }
    struct termios tty = *initial_tty; // copy for editing

    // Configure baud rate (115200)
    cfsetispeed(&tty, B115200);  // Input speed
    cfsetospeed(&tty, B115200);  // Output speed

    // Configure 8N1 (8 data bits, no parity, 1 stop bit)
    tty.c_cflag &= ~PARENB;  // Clear parity bit (no parity)
    tty.c_cflag &= ~CSTOPB;  // Clear stop bit (1 stop bit)
    tty.c_cflag &= ~CSIZE;   // Clear data size bits
    tty.c_cflag |= CS8;      // Set 8 data bits

    // Disable hardware flow control
    #ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
    #elif defined(CNEW_RTSCTS)
    tty.c_cflag &= ~CNEW_RTSCTS;
    #endif

    tty.c_cflag |= CREAD | CLOCAL;  // Turn on READ & ignore modem control lines

    // Set RAW mode (binary communication, no line processing)
    tty.c_lflag &= ~ICANON;  // Disable canonical mode (line-by-line)
    tty.c_lflag &= ~ECHO;    // Disable echo
    tty.c_lflag &= ~ECHOE;   // Disable erasure
    tty.c_lflag &= ~ECHONL;  // Disable new-line echo
    tty.c_lflag &= ~ISIG;    // Disable interpretation of INTR, QUIT and SUSP

    // Disable software flow control
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    
    // Disable special handling of received bytes
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    
    // Prevent special interpretation of output bytes
    tty.c_oflag &= ~OPOST;  // Disable output processing
    tty.c_oflag &= ~ONLCR;  // Prevent conversion of newline to carriage return/line feed

    // Set timeout configuration
    // VMIN = 0, VTIME > 0: Timeout with no minimum bytes
    // Read returns when data arrives or timeout expires
    tty.c_cc[VMIN] = 0;   // No minimum
    tty.c_cc[VTIME] = 10; // 1 second timeout (tenths of a second)

    // Apply settings
    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
        fprintf(stderr, "Error %i from tcsetattr: %s\n", errno, strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/**
 * Restores serial port to original settings
 * 
 * @param fd The file descriptor of the serial port
 * @param settings The original termios settings to restore
 */
void restore_serial(int fd, const struct termios *settings) {
    if (tcsetattr(fd, TCSANOW, settings) != 0 && fatal_error_in_progress == 0) {
        fprintf(stderr, "Error %i restoring settings for fd %d: %s\n", 
                errno, fd, strerror(errno));
    }
}

/**
 * Closes all serial ports and restores their initial settings
 * Goes in reverse order to ensure vna count consistency
 */
void close_and_reset_all() {
    while (VNA_COUNT_GLOBAL > 0) {
        int i = VNA_COUNT_GLOBAL-1;
        // Restore original settings before closing
        restore_serial(SERIAL_PORTS[i], &INITIAL_PORT_SETTINGS[i]);
        
        // Close the serial port
        if (close(SERIAL_PORTS[i]) != 0 && !fatal_error_in_progress) {
            fprintf(stderr, "Error %i closing port %d: %s\n", 
                    errno, i, strerror(errno));
        }
        VNA_COUNT_GLOBAL--;
    }
}

/**
 * Writes a command to the serial port with error checking
 * 
 * @param fd The file descriptor of the serial port
 * @param cmd The command string to send (should include \r terminator)
 * @return Number of bytes written on success, -1 on error
 */
ssize_t write_command(int fd, const char *cmd) {
    size_t cmd_len = strlen(cmd);
    ssize_t bytes_written = write(fd, cmd, cmd_len);
    
    if (bytes_written < 0) {
        fprintf(stderr, "Error writing to fd %d: %s\n", fd, strerror(errno));
        return -1;
    } else if (bytes_written < (ssize_t)cmd_len) {
        fprintf(stderr, "Warning: Partial write (%zd of %zu bytes) on fd %d\n", 
                bytes_written, cmd_len, fd);
    }
    
    return bytes_written;
}

/**
 * Reads exact number of bytes from serial port
 * Handles partial reads by continuing until all bytes are received
 * 
 * @param fd The file descriptor of the serial port
 * @param buffer The buffer to read data into
 * @param length The number of bytes to read
 * @return Number of bytes read on success, -1 on error, 0 on timeout
 */
ssize_t read_exact(int fd, uint8_t *buffer, size_t length) {
    ssize_t bytes_read = 0;
    
    while (bytes_read < (ssize_t)length) {
        ssize_t n = read(fd, buffer + bytes_read, length - bytes_read);
        
        if (n < 0) {
            fprintf(stderr, "Error reading from fd %d: %s\n", fd, strerror(errno));
            return -1;
        } else if (n == 0) {
            // Timeout or end of file
            if (bytes_read > 0) {
                fprintf(stderr, "Timeout: only read %zd of %zu bytes from fd %d\n", 
                        bytes_read, length, fd);
            }
            return bytes_read;
        }
        
        bytes_read += n;
    }
    
    return bytes_read;
}

/**
 * Finds the binary header in the serial stream
 * Scans byte-by-byte looking for the header pattern (mask + points)
 * 
 * @param fd The file descriptor of the serial port
 * @param expected_mask The expected mask value (e.g., 135)
 * @param expected_points The expected points value (e.g., 101)
 * @return 1 if header found, 0 if timeout/not found, -1 on error
 */
int find_binary_header(int fd, uint16_t expected_mask, uint16_t expected_points) {
    uint8_t window[4] = {0};
    int max_bytes = 500;  // Maximum bytes to scan before giving up
    
    // Read initial 4 bytes
    if (read_exact(fd, window, 4) != 4) {
        fprintf(stderr, "Failed to read initial header bytes\n");
        return EXIT_FAILURE;
    }
    
    // Check if we already have the header
    uint16_t mask = window[0] | (window[1] << 8);
    uint16_t points = window[2] | (window[3] << 8);
    if (mask == expected_mask && points == expected_points) {
        return EXIT_SUCCESS;
    }
    
    // Scan through the stream byte by byte
    for (int i = 4; i < max_bytes; i++) {
        uint8_t byte;
        ssize_t n = read(fd, &byte, 1);
        
        if (n < 0) {
            fprintf(stderr, "Error reading header byte: %s\n", strerror(errno));
            return EXIT_FAILURE;
        } else if (n == 0) {
            fprintf(stderr, "Timeout waiting for binary header\n");
            return EXIT_FAILURE;
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
            return EXIT_SUCCESS;  // Found header
        }
    }
    
    fprintf(stderr, "Binary header not found after %d bytes\n", max_bytes);
    return EXIT_FAILURE;
}

/**
 * Bounded Buffer handling functions (do not access bounded buffer otherwise)
 */
int create_bounded_buffer(BoundedBuffer *bb) {
    struct datapoint_nanoVNA_H **buffer = malloc(sizeof(struct datapoint_nanoVNA_H *)*N);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer memory\n");
        return EXIT_FAILURE;
    }
    *bb = (BoundedBuffer){buffer,0,0,0,0,PTHREAD_COND_INITIALIZER,PTHREAD_COND_INITIALIZER,PTHREAD_MUTEX_INITIALIZER};
    return EXIT_SUCCESS;
}

void destroy_bounded_buffer(BoundedBuffer *buffer) {
    free(buffer->buffer);
    buffer->buffer = NULL;
    free(buffer);
    buffer = NULL;
}

void add_buff(BoundedBuffer *buffer, struct datapoint_nanoVNA_H *data) {
    pthread_mutex_lock(&buffer->lock);
    while (buffer->count == N) {
        pthread_cond_wait(&buffer->take_cond, &buffer->lock);
    }
    buffer->buffer[buffer->in] = data;
    buffer->in = (buffer->in+1) % N;
    buffer->count++;
    pthread_cond_signal(&buffer->add_cond);
    pthread_mutex_unlock(&buffer->lock);
    return;
}

struct datapoint_nanoVNA_H* take_buff(BoundedBuffer *buffer) {
    pthread_mutex_lock(&buffer->lock);
    while (buffer->count == 0) {
        if (buffer->complete >= VNA_COUNT_GLOBAL) {
            pthread_mutex_unlock(&buffer->lock);
            return NULL;
        }
        pthread_cond_wait(&buffer->add_cond, &buffer->lock);
    }
    struct datapoint_nanoVNA_H *data = buffer->buffer[buffer->out];
    buffer->buffer[buffer->out] = NULL;
    buffer->out = (buffer->out + 1) % N;
    buffer->count--;
    pthread_cond_signal(&buffer->take_cond);
    pthread_mutex_unlock(&buffer->lock);
    return data;
}

/**
 * producer / consumer functions
 */
struct datapoint_nanoVNA_H* pull_scan(int port, int vnaID, int start, int stop) {
    struct timeval send_time, receive_time;
    gettimeofday(&send_time, NULL);

    // Send scan command
    char msg_buff[50];
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, stop, POINTS, MASK);
    if (write_command(port, msg_buff) < 0) {
        fprintf(stderr, "Failed to send scan command\n");
        return NULL;
    }

    // Find binary header in response
    int header_found = find_binary_header(port, MASK, POINTS);
    if (header_found != EXIT_SUCCESS) {
        fprintf(stderr, "Failed to find binary header\n");
        return NULL;
    }

    // Receive data points
    struct datapoint_nanoVNA_H *data = malloc(sizeof(struct datapoint_nanoVNA_H));
    if (!data) {
        fprintf(stderr, "Failed to allocate memory for data points\n");
        return NULL;
    }

    for (int i = 0; i < POINTS; i++) {
        // Read raw data (20 bytes from NanoVNA)
        ssize_t bytes_read = read_exact(port, 
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
    data->vna_id = vnaID;
    // Set Timestamps
    gettimeofday(&receive_time, NULL);
    data->send_time = send_time;
    data->receive_time = receive_time;
    return data;
}

void* scan_producer_num(void *arguments) {

    struct scan_producer_args *args = (struct scan_producer_args*)arguments;

    for (int sweep = 0; sweep < args->nbr_sweeps && async_scan_active; sweep++) {
        if (args->nbr_sweeps > 1) {
            printf("[Producer] Starting sweep %d/%d\n", sweep + 1, args->nbr_sweeps);
        }

        int current = args->start;
        int step = (int)round(args->stop - args->start) / ((args->nbr_scans*POINTS)-1);
        for (int scan = 0; scan < args->nbr_scans && async_scan_active; scan++) {
            struct datapoint_nanoVNA_H *data = pull_scan(args->serial_port,args->vna_id,
                                                        current,current + step*(POINTS-1));
            // add to buffer
            if (data) {add_buff(args->bfr,data);}

            current += step*POINTS;
        }
    }
    args->bfr->complete++;
    return NULL;
}
void* scan_producer_time(void *arguments) {

    struct scan_producer_args *args = (struct scan_producer_args*)arguments;

    int sweep = 1;
    while (args->bfr->complete == 0 && async_scan_active) {
        sweep++;
        if (args->nbr_sweeps > 1) {
            printf("[Producer] Starting sweep %d\n", sweep + 1);
        }
        int total_scans = args->nbr_scans;
        int step = (args->stop - args->start) / total_scans;
        int current = args->start;
        while (total_scans > 0 && async_scan_active) {
            struct datapoint_nanoVNA_H *data = pull_scan(args->serial_port,args->vna_id,
                                                        current,current + step);
            // add to buffer
            if (data) {add_buff(args->bfr,data);}

            // finish loop
            total_scans--;
            current += step;
        }       
    }
    return NULL;
}

void* scan_timer(void *arguments) { 
    struct scan_timer_args *args = arguments;
    sleep(args->time_to_wait);
    
    // Signal completion for both sync and async modes
    args->b->complete = VNA_COUNT_GLOBAL;
    
    // Only modify async state if we're actually in async mode
    if (async_scan_active) {
        async_scan_active = 0;
        
        // Notify GUI through status callback
        if (async_status_callback) {
            async_status_callback("Timer completed - finishing scan");
        }
    }
    
    return NULL;
}

void* scan_consumer(void *arguments) {

    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;
    int total_count = 0;

    while (args->bfr->complete < VNA_COUNT_GLOBAL || (args->bfr->count != 0)) {

        struct datapoint_nanoVNA_H *data = take_buff(args->bfr);
        if (!data) {
            // take_buff has returned nothing as there was nothing left to take
            return NULL;
        }

        for (int i = 0; i < POINTS; i++) {
            
            printf("VNA%d (%d) s:%lf r:%lf | %u Hz: S11=%f+%fj, S21=%f+%fj\n", 
                   data->vna_id, total_count,
                   ((double)(data->send_time.tv_sec - program_start_time.tv_sec) + (double)(data->send_time.tv_usec - program_start_time.tv_usec) / 1000000.0),
                   ((double)(data->receive_time.tv_sec - program_start_time.tv_sec) + (double)(data->receive_time.tv_usec - program_start_time.tv_usec) / 1000000.0),
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

void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, const char **ports){
    // Reset VNA_COUNT for clean state on subsequent runs
    VNA_COUNT_GLOBAL = 0;

    // Initialise global variables
    SERIAL_PORTS = calloc(num_vnas, sizeof(int));
    INITIAL_PORT_SETTINGS = calloc(num_vnas, sizeof(struct termios));
    
    if (!SERIAL_PORTS || !INITIAL_PORT_SETTINGS) {
        fprintf(stderr, "Failed to allocate memory for serial port arrays\n");
        if (SERIAL_PORTS) {free(SERIAL_PORTS);SERIAL_PORTS = NULL;}
        if (INITIAL_PORT_SETTINGS) {free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;}
        return;
    }

    gettimeofday(&program_start_time, NULL);

    int error;

    // Create consumer and producer threads
    BoundedBuffer *bounded_buffer = malloc(sizeof(BoundedBuffer));
    if (!bounded_buffer) {
        fprintf(stderr, "Failed to allocate memory for bounded buffer construct\n");
        free(SERIAL_PORTS);SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;
        return;
    }
    error = create_bounded_buffer(bounded_buffer);
    if (error != 0) {
        fprintf(stderr, "Failed to create bounded buffer\n");
        free(bounded_buffer);
        free(SERIAL_PORTS);SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;
        return;
    }
    
    struct scan_producer_args arguments[num_vnas];
    pthread_t producers[num_vnas];
    for(int i = 0; i < num_vnas; i++) {
        // Open serial port with error checking
        SERIAL_PORTS[i] = open_serial(ports[i]);
        if (SERIAL_PORTS[i] < 0) {
            fprintf(stderr, "Failed to open serial port for VNA %d\n", i);
            // Clean up already opened ports
            for (int j = 0; j < i; j++) {
                close(SERIAL_PORTS[j]);
            }
            destroy_bounded_buffer(bounded_buffer);
            free(SERIAL_PORTS);SERIAL_PORTS = NULL;
            free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;
            return;
        }
        
        // Configure serial port and save original settings
        error = configure_serial(SERIAL_PORTS[i],&INITIAL_PORT_SETTINGS[i]);
        if (error != 0) {
            fprintf(stderr, "Failed to configure port settings for VNA %d\n", i);
            // Clean up already opened ports
            for (int j = 0; j < i; j++) {
                restore_serial(SERIAL_PORTS[j], &INITIAL_PORT_SETTINGS[j]);
            }
            for (int j = 0; j < num_vnas; j++) {
                close(SERIAL_PORTS[j]);
            }
            destroy_bounded_buffer(bounded_buffer);
            free(SERIAL_PORTS);SERIAL_PORTS = NULL;
            free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;
            return;
        }
        VNA_COUNT_GLOBAL++;

        arguments[i].vna_id = i;
        arguments[i].serial_port = SERIAL_PORTS[i];
        arguments[i].nbr_scans = nbr_scans;
        arguments[i].start = start;
        arguments[i].stop = stop;
        arguments[i].nbr_sweeps = sweeps;
        arguments[i].bfr = bounded_buffer;

        if (sweep_mode == NUM_SWEEPS) {
            error = pthread_create(&producers[i], NULL, &scan_producer_num, &arguments[i]);
        } else if (sweep_mode == TIME) {
            error = pthread_create(&producers[i], NULL, &scan_producer_time, &arguments[i]);
        }
        if(error != 0){
            fprintf(stderr, "Error %i creating producer thread %d: %s\n", errno, i, strerror(errno));
            return;
        }
    }

    pthread_t consumer;
    struct scan_consumer_args consumer_args = {bounded_buffer};
    error = pthread_create(&consumer, NULL, &scan_consumer, &consumer_args);
    if(error != 0){
        fprintf(stderr, "Error %i creating consumer thread: %s\n", errno, strerror(errno));
        destroy_bounded_buffer(bounded_buffer);bounded_buffer=NULL;
        close_and_reset_all();
        free(SERIAL_PORTS);SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;
        return;
    }

    if (sweep_mode == TIME) {
        pthread_t timer;
        struct scan_timer_args timer_args= (struct scan_timer_args){sweeps,bounded_buffer};
        error = pthread_create(&timer, NULL, &scan_timer, &timer_args);
        if(error != 0){
            fprintf(stderr, "Error %i creating timer thread: %s\n", errno, strerror(errno));
            return;
        }
        error = pthread_join(timer, NULL);
        if(error != 0){printf("Error %i from join timer:\n", errno);}
    }

    // wait for threads to finish

    for(int i = 0; i < num_vnas; i++) {
        error = pthread_join(producers[i], NULL);
        if(error != 0){printf("Error %i from join producer:\n", errno);}
    }

    error = pthread_join(consumer,NULL);
    if(error != 0){printf("Error %i from join consumer:\n", errno);}

    // finish up
    destroy_bounded_buffer(bounded_buffer);

    close_and_reset_all();
    free(SERIAL_PORTS);
    SERIAL_PORTS = NULL;
    free(INITIAL_PORT_SETTINGS);
    INITIAL_PORT_SETTINGS = NULL;

    return;
}

/**
 * Helper function. Issues info command and prints output
 * 
 * @param serial_port The file descriptor of the serial port
 * @return 0 on success, 1 on error
 */
int test_connection(int serial_port) {
    int numBytes;
    char buffer[32];

    const char *msg = "info\r";
    if (write_command(serial_port, msg) < 0) {
        fprintf(stderr, "Failed to send info command\n");
        return 1;
    }

    do {
        numBytes = read(serial_port,&buffer,sizeof(char)*31);
        if (numBytes < 0) {printf("Error reading: %s", strerror(errno));return 1;}
        buffer[numBytes] = '\0';
        printf("%s", (unsigned char*)buffer);
    } while (numBytes > 0 && !strstr(buffer,"ch>"));

    return 0;
}

// ------------------------------------
// PYTHON API
// ------------------------------------


// Test Function
int fill_test_datapoint(DataPoint *point){
    if (!point) return 1; // error: null pointer

    point->vna_id = 1;
    point->frequency = 12345678;
    point->s11_re = 1.0f;
    point->s11_im = 0.0f;
    point->s21_re = 0.5f;
    point->s21_im = -0.5f;
    point->sweep_number = 42;
    point->send_time = 1000.0;
    point->receive_time = 1001.0;

    return 0; // success
}

/**
 * Convert internal data structure to Python-friendly DataPoint
 */
static void convert_to_datapoint(struct datapoint_nanoVNA_H* internal_data, 
                                  DataPoint* output, int point_index) {
    if (!internal_data || !output || point_index < 0 || point_index >= POINTS) {
        return;
    }
    
    struct nanovna_raw_datapoint* raw = &internal_data->point[point_index];
    
    output->vna_id = internal_data->vna_id;
    output->frequency = raw->frequency;
    output->s11_re = raw->s11.re;
    output->s11_im = raw->s11.im;
    output->s21_re = raw->s21.re;
    output->s21_im = raw->s21.im;
    output->sweep_number = 0;
    output->send_time = (double)internal_data->send_time.tv_sec + 
                        (double)internal_data->send_time.tv_usec / 1000000.0;
    output->receive_time = (double)internal_data->receive_time.tv_sec + 
                          (double)internal_data->receive_time.tv_usec / 1000000.0;
}

/**
 * Modified consumer thread that calls Python callbacks instead of printing
 */
static void* async_scan_consumer(void *arguments) {
    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;
    int total_count = 0;
    
    while (async_scan_active && (args->bfr->complete < VNA_COUNT_GLOBAL || args->bfr->count != 0)) {
        struct datapoint_nanoVNA_H *data = take_buff(args->bfr);
        
        if (!data) {
            return NULL;
        }
        
        // Convert and send each point via callback
        for (int i = 0; i < POINTS && async_scan_active; i++) {
            DataPoint dp;
            convert_to_datapoint(data, &dp, i);
            dp.sweep_number = total_count / POINTS;
            
            if (async_data_callback) {
                async_data_callback(&dp);
            }
            
            total_count++;
        }
        
        free(data);
        data = NULL;
    }
    
    if (async_status_callback) {
        async_status_callback("Scan consumer thread finished");
    }
    
    return NULL;
}

/**
 * Background thread function for async scanning
 */
static void* async_scan_thread_func(void *arguments) {
    struct {
        int num_vnas;
        int nbr_scans;
        int start;
        int stop;
        SweepMode sweep_mode;
        int sweeps;
        const char **ports;
    } *args = (void*)arguments;
    
    // Declare variable-length arrays first to avoid goto issues
    struct scan_producer_args producer_args[args->num_vnas];
    pthread_t producers[args->num_vnas];
    BoundedBuffer *bounded_buffer = NULL;
    
    // Similar setup to run_multithreaded_scan but with async consumer
    VNA_COUNT_GLOBAL = 0;
    SERIAL_PORTS = calloc(args->num_vnas, sizeof(int));
    INITIAL_PORT_SETTINGS = calloc(args->num_vnas, sizeof(struct termios));
    
    if (!SERIAL_PORTS || !INITIAL_PORT_SETTINGS) {
        if (async_error_callback) {
            async_error_callback("Failed to allocate memory for serial port arrays");
        }
        goto cleanup;
    }
    
    gettimeofday(&program_start_time, NULL);
    
    bounded_buffer = malloc(sizeof(BoundedBuffer));
    if (!bounded_buffer || create_bounded_buffer(bounded_buffer) != EXIT_SUCCESS) {
        if (async_error_callback) {
            async_error_callback("Failed to create bounded buffer");
        }
        goto cleanup;
    }
    
    for (int i = 0; i < args->num_vnas && async_scan_active; i++) {
        SERIAL_PORTS[i] = open_serial(args->ports[i]);
        if (SERIAL_PORTS[i] < 0) {
            if (async_error_callback) {
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "Failed to open port %s", args->ports[i]);
                async_error_callback(err_msg);
            }
            goto cleanup;
        }
        
        if (configure_serial(SERIAL_PORTS[i], &INITIAL_PORT_SETTINGS[i]) != EXIT_SUCCESS) {
            if (async_error_callback) {
                async_error_callback("Failed to configure serial port");
            }
            goto cleanup;
        }
        VNA_COUNT_GLOBAL++;
    }
    
    // All ports opened successfully - now report actual success
    if (async_status_callback) {
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "✓ Scan started successfully - %d VNA(s) connected", args->num_vnas);
        async_status_callback(status_msg);
    }
    
    // Start producer threads
    for (int i = 0; i < args->num_vnas && async_scan_active; i++) {
        producer_args[i].vna_id = i;
        producer_args[i].serial_port = SERIAL_PORTS[i];
        producer_args[i].nbr_scans = args->nbr_scans;
        producer_args[i].start = args->start;
        producer_args[i].stop = args->stop;
        producer_args[i].nbr_sweeps = args->sweeps;
        producer_args[i].bfr = bounded_buffer;
        
        if (args->sweep_mode == NUM_SWEEPS) {
            pthread_create(&producers[i], NULL, &scan_producer_num, &producer_args[i]);
        } else {
            pthread_create(&producers[i], NULL, &scan_producer_time, &producer_args[i]);
        }
    }
    
    // Create and start consumer thread
    pthread_t consumer;
    struct scan_consumer_args consumer_args = {bounded_buffer};
    pthread_create(&consumer, NULL, &async_scan_consumer, &consumer_args);
    
    // Optional: timer for TIME mode
    pthread_t timer = 0;
    if (args->sweep_mode == TIME) {
        struct scan_timer_args timer_args = {args->sweeps, bounded_buffer};
        pthread_create(&timer, NULL, &scan_timer, &timer_args);
    }
    
    // Wait for threads to complete
    for (int i = 0; i < args->num_vnas; i++) {
        pthread_join(producers[i], NULL);
    }
    
    if (timer) {
        pthread_join(timer, NULL);
    }
    
    pthread_join(consumer, NULL);
    
    if (async_status_callback) {
        async_status_callback("Scan completed successfully");
    }

cleanup:
    // Cleanup
    if (bounded_buffer) {
        destroy_bounded_buffer(bounded_buffer);
    }
    close_and_reset_all();
    free(SERIAL_PORTS);
    SERIAL_PORTS = NULL;
    free(INITIAL_PORT_SETTINGS);
    INITIAL_PORT_SETTINGS = NULL;
    
    async_scan_active = 0;
    free(args);
    
    return NULL;
}

/**
 * Start an asynchronous VNA scan with Python callbacks
 */
int start_async_scan(int num_vnas, int nbr_scans, int start, int stop,
                     int sweep_mode, int sweeps_or_time, const char **ports,
                     DataCallback data_cb, StatusCallback status_cb, ErrorCallback error_cb) {
    
    if (async_scan_active) {
        if (error_cb) {
            error_cb("Scan already running");
        }
        return -1;
    }
    
    // Brief wait to allow previous detached thread to fully clean up
    if (async_thread_created) {
        usleep(100000);  // Wait 100ms
    }
    
    // Validate inputs
    if (num_vnas <= 0 || nbr_scans <= 0 || !ports || !data_cb) {
        if (error_cb) {
            error_cb("Invalid parameters");
        }
        return -1;
    }
    
    // Store callbacks
    async_data_callback = data_cb;
    async_status_callback = status_cb;
    async_error_callback = error_cb;
    async_scan_active = 1;
    
    // Allocate and setup thread args
    typedef struct {
        int num_vnas;
        int nbr_scans;
        int start;
        int stop;
        SweepMode sweep_mode;
        int sweeps;
        const char **ports;
    } ThreadArgs;
    
    ThreadArgs *args = malloc(sizeof(ThreadArgs));
    if (!args) {
        async_scan_active = 0;
        if (error_cb) {
            error_cb("Failed to allocate thread arguments");
        }
        return -1;
    }
    
    args->num_vnas = num_vnas;
    args->nbr_scans = nbr_scans;
    args->start = start;
    args->stop = stop;
    args->sweep_mode = (SweepMode)sweep_mode;
    args->sweeps = sweeps_or_time;
    args->ports = ports;
    
    // Create detached thread for async scanning
    if (pthread_create(&async_scan_thread, NULL, &async_scan_thread_func, args) != 0) {
        async_scan_active = 0;
        free(args);
        if (error_cb) {
            error_cb("Failed to create scan thread");
        }
        return -1;
    }
    
    // Detach thread so it auto-cleans up when it exits
    pthread_detach(async_scan_thread);
    
    // Mark that we've successfully created a thread
    async_thread_created = 1;
    
    // Status callback will be sent after ports are actually opened
    return 0;
}

/**
 * Stop the currently running async scan
 */
int stop_async_scan(void) {
    if (!async_scan_active) {
        return 0;
    }
    
    async_scan_active = 0;
    sleep(1);
    
    return 0;
}

/**
 * Check if an async scan is currently running
 */
int is_async_scan_active(void) {
    return async_scan_active ? 1 : 0;
}

