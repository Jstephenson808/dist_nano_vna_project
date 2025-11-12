#include "vnaScanMultithreaded.h"
static volatile sig_atomic_t fatal_error_in_progress = 0; // For proper SIGINT handling
volatile atomic_int complete = 0; 

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
struct termios configure_serial(int serial_port) {
    struct termios initial_tty; // keep to restore settings later
    if (tcgetattr(serial_port, &initial_tty) != 0) {
        fprintf(stderr, "Error %i from tcgetattr: %s\n", errno, strerror(errno));
    }
    struct termios tty = initial_tty; // copy for editing

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
    tty.c_cc[VMIN] = 0;   // Read doesn't block (return immediately with available data)
    tty.c_cc[VTIME] = 10; // 1 second timeout (tenths of a second)

    // Apply settings
    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
        fprintf(stderr, "Error %i from tcsetattr: %s\n", errno, strerror(errno));
    }

    return initial_tty; // Return original settings for restoration
}

/**
 * Restores serial port to original settings
 * 
 * @param fd The file descriptor of the serial port
 * @param settings The original termios settings to restore
 */
void restore_serial(int fd, const struct termios *settings) {
    if (tcsetattr(fd, TCSANOW, settings) != 0) {
        fprintf(stderr, "Error %i restoring settings for fd %d: %s\n", 
                errno, fd, strerror(errno));
    }
}

/**
 * Closes all serial ports and restores their initial settings
 * Loops through ports in reverse order to maintain VNA_COUNT accuracy
 */
void close_and_reset_all() {
    for (int i = VNA_COUNT-1; i >= 0; i--) {
        // Restore original settings before closing
        restore_serial(SERIAL_PORTS[i], &INITIAL_PORT_SETTINGS[i]);
        
        // Close the serial port
        if (close(SERIAL_PORTS[i]) != 0) {
            fprintf(stderr, "Error %i closing port %d: %s\n", 
                    errno, i, strerror(errno));
        }
        
        VNA_COUNT--;
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
        return -1;
    }
    
    // Check if we already have the header
    uint16_t mask = window[0] | (window[1] << 8);
    uint16_t points = window[2] | (window[3] << 8);
    if (mask == expected_mask && points == expected_points) {
        return 1;
    }
    
    // Scan through the stream byte by byte
    for (int i = 4; i < max_bytes; i++) {
        uint8_t byte;
        ssize_t n = read(fd, &byte, 1);
        
        if (n < 0) {
            fprintf(stderr, "Error reading header byte: %s\n", strerror(errno));
            return -1;
        } else if (n == 0) {
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

    int total_scans = args->points / POINTS;
    int step = (args->stop - args->start) / total_scans;
    int current = args->start;

    while (total_scans > 0) {

        // Send scan command
        char msg_buff[50];
        snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", 
                 current, (int)round(current + step), POINTS, MASK);
        
        if (write_command(args->serial_port, msg_buff) < 0) {
            fprintf(stderr, "Failed to send scan command\n");
            return NULL;
        }

        // Find binary header in response
        int header_found = find_binary_header(args->serial_port, MASK, POINTS);
        if (header_found != 1) {
            fprintf(stderr, "Failed to find binary header\n");
            return NULL;
        }

        // Receive data points
        struct datapoint_NanoVNAH *data = malloc(sizeof(struct datapoint_NanoVNAH) * POINTS);
        if (!data) {
            fprintf(stderr, "Failed to allocate memory for data points\n");
            return NULL;
        }

        for (int i = 0; i < POINTS; i++) {
            ssize_t bytes_read = read_exact(args->serial_port, 
                                           (uint8_t*)(data + i), 
                                           sizeof(struct datapoint_NanoVNAH));
            if (bytes_read != sizeof(struct datapoint_NanoVNAH)) {
                fprintf(stderr, "Error reading data point %d: got %zd bytes, expected %zu\n", 
                        i, bytes_read, sizeof(struct datapoint_NanoVNAH));
                free(data);
                return NULL;
            }
        }

        // add to buffer

        pthread_mutex_lock(&args->thread_args->lock);
        while (args->thread_args->count == N) {
            pthread_cond_wait(&args->thread_args->remove_cond, &args->thread_args->lock);
        }
        args->buffer[args->thread_args->in] = data;
        args->thread_args->in = (args->thread_args->in+1) % N;
        args->thread_args->count++;
        pthread_cond_signal(&args->thread_args->fill_cond);
        pthread_mutex_unlock(&args->thread_args->lock);

        // finish loop

        total_scans--;
        current += step;
    }
    complete++;
    return NULL;
}

void* scan_consumer(void *arguments) {

    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;
    int total_count = 0;

    while (complete < VNA_COUNT || (args->thread_args->count != 0)) {
        pthread_mutex_lock(&args->thread_args->lock);
        while (args->thread_args->count == 0) {
            pthread_cond_wait(&args->thread_args->fill_cond, &args->thread_args->lock);
        }
        struct datapoint_NanoVNAH *data = args->buffer[args->thread_args->out];
        args->buffer[args->thread_args->out] = NULL;
        args->thread_args->out = (args->thread_args->out + 1) % N;
        args->thread_args->count--;
        pthread_cond_signal(&args->thread_args->remove_cond);
        pthread_mutex_unlock(&args->thread_args->lock);

        for (int i = 0; i < POINTS; i++) {
            printf("(%d) %u Hz: S11=%f+%fj, S21=%f+%fj\n", total_count, data[i].frequency, data[i].s11.re, data[i].s11.im, data[i].s21.re, data[i].s21.im);
            total_count++;
        }

        free(data);
        data = NULL;
    }
    return NULL;
}

void run_multithreaded_scan(int num_vnas, int points, int start, int stop) {
    // Initialise global variables
    SERIAL_PORTS = calloc(num_vnas, sizeof(int));
    INITIAL_PORT_SETTINGS = calloc(num_vnas, sizeof(struct termios));
    
    if (!SERIAL_PORTS || !INITIAL_PORT_SETTINGS) {
        fprintf(stderr, "Failed to allocate memory for serial port arrays\n");
        if (SERIAL_PORTS) {free(SERIAL_PORTS);SERIAL_PORTS = NULL;}
        if (INITIAL_PORT_SETTINGS) {free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;}
        return;
    }

    // Create consumer and producer threads
    struct datapoint_NanoVNAH **buffer = malloc(sizeof(struct datapoint_NanoVNAH *)*(N+1));
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer memory\n");
        free(SERIAL_PORTS);SERIAL_PORTS=NULL;
        free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS=NULL;
        return;
    }
    
    struct coordination_args thread_args = {0,0,0,PTHREAD_COND_INITIALIZER,PTHREAD_COND_INITIALIZER,PTHREAD_MUTEX_INITIALIZER};
    complete = 0;

    int error;

    // warning: needs work done before this will work properly >1 VNA
    struct scan_producer_args arguments[num_vnas];
    pthread_t producers[num_vnas];
    for(int i = 0; i < num_vnas; i++) {
        // Open serial port with error checking
        SERIAL_PORTS[i] = open_serial("/dev/ttyACM0"); // Will need logic to decide port
        if (SERIAL_PORTS[i] < 0) {
            fprintf(stderr, "Failed to open serial port for VNA %d\n", i);
            // Clean up already opened ports
            for (int j = 0; j < i; j++) {
                restore_serial(SERIAL_PORTS[j], &INITIAL_PORT_SETTINGS[j]);
                close(SERIAL_PORTS[j]);
            }
            free(buffer);buffer = NULL;
            free(SERIAL_PORTS);SERIAL_PORTS = NULL;
            free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;
            return;
        }
        
        // Configure serial port and save original settings
        INITIAL_PORT_SETTINGS[i] = configure_serial(SERIAL_PORTS[i]);
        VNA_COUNT++;

        arguments[i].serial_port = SERIAL_PORTS[i];
        arguments[i].points = points;
        arguments[i].start = start;
        arguments[i].stop = stop;
        arguments[i].buffer = buffer;
        arguments[i].thread_args = &thread_args;

        error = pthread_create(&producers[i], NULL, &scan_producer, &arguments[i]);
        if(error != 0){
            fprintf(stderr, "Error %i creating producer thread %d: %s\n", errno, i, strerror(errno));
            return;
        }
    }

    pthread_t consumer;
    struct scan_consumer_args consumer_args = {buffer,&thread_args};
    error = pthread_create(&consumer, NULL, &scan_consumer, &consumer_args);
    if(error != 0){
        fprintf(stderr, "Error %i creating consumer thread: %s\n", errno, strerror(errno));
        free(buffer);buffer = NULL;
        free(SERIAL_PORTS);SERIAL_PORTS = NULL;
        free(INITIAL_PORT_SETTINGS);INITIAL_PORT_SETTINGS = NULL;
        return;
    }

    // wait for threads to finish

    for(int i = 0; i < num_vnas; i++) {
        error = pthread_join(producers[i], NULL);
        if(error != 0){printf("Error %i from join producer:\n", errno);return;}
    }

    error = pthread_join(consumer,NULL);
    if(error != 0){printf("Error %i from join consumer:\n", errno);return;}

    // finish up
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

/*
 * Sends NanoVNA a scan command, prints formatted output
 * 
 * Output is recieved in binary, in frames as specified by the datapoint struct.
 * Output can have echoes and unexpected elements. When searching for the start of a 
 * specified output ("skip to binary header"), uses characters to represent bytes being
 * read and scans one byte at a time to match binary header pattern.
 * Scanning is done with larger buffers at all other points, as 1 byte buffers are inefficient.
 */
int main(int argc, char *argv[]) {
    // defaults
    long start_freq = 50000000;
    long stop_freq = 900000000;
    int nbr_points = 10100;
    int nbr_nanoVNAs = 1;
    int nbr_sweeps = 1;

    

    if (argc > 1) {
        if (argc != 6) {
            fprintf(stderr, "Usage: %s <start_freq> <stop_freq> <nbr_points> <nbr_nanoVNAs> <nbr_sweeps>\n", argv[0]);
            fprintf(stderr, "Example: %s 50000000 900000000 10100 1 1\n", argv[0]);
            fprintf(stderr, "Run without arguments for default scan\n");
            return EXIT_FAILURE;
        }

        start_freq = atol(argv[1]);
        stop_freq = atol(argv[2]);
        nbr_points = atoi(argv[3]);
        nbr_nanoVNAs = atoi(argv[4]);
        nbr_sweeps = atoi(argv[5]);

        // Validation of arguments
        if (start_freq <= 0 || stop_freq <= 0 || nbr_points <= 0 || 
            nbr_nanoVNAs <= 0 || nbr_sweeps <= 0) {
            fprintf(stderr, "Error: All arguments must be positive numbers\n");
            return EXIT_FAILURE;
        }
        
        if (start_freq >= stop_freq) {
            fprintf(stderr, "Error: start_freq must be less than stop_freq\n");
            return EXIT_FAILURE;
        }
    } else {
        printf("Running default scan: %ld Hz to %ld Hz, %d points, %d VNA(s), %d sweep(s)\n",
               start_freq, stop_freq, nbr_points, nbr_nanoVNAs, nbr_sweeps);
    }

    // assign error handler
    if (signal(SIGINT, fatal_error_signal) == SIG_ERR) {
        fprintf(stderr, "An error occurred while setting a signal handler.\n");
        return EXIT_FAILURE;
    }

    // start timing
    struct timeval stop, start;
    gettimeofday(&start, NULL);

    // call a scan (with one nanoVNA)
    for (int sweep = 0; sweep < nbr_sweeps; sweep++) {
        if (nbr_sweeps > 1) {
            printf("=== Sweep %d of %d ===\n", sweep + 1, nbr_sweeps);
        }
        run_multithreaded_scan(nbr_nanoVNAs, nbr_points, start_freq, stop_freq);
    }
    

    // finish timing
    gettimeofday(&stop, NULL);
    double elapsed = (double)(stop.tv_sec - start.tv_sec) + 
                     (double)(stop.tv_usec - start.tv_usec) / 1000000.0;
    
    int total_points = nbr_points * nbr_sweeps;
    printf("---\ntook %.6f s\n", elapsed);
    printf("%.6f s per point measurement\n", elapsed / total_points);
    printf("%.2f points per second\n", total_points / elapsed);

    return EXIT_SUCCESS;
}