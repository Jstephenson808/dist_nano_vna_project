#include "VnaCommunication.h"

int num_vnas = 0;
char **ports = NULL;

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
 * 
 * @return EXIT_SUCCESS on success, errno on error
 */
int restore_serial(int fd, const struct termios *settings) {
    if (tcsetattr(fd, TCSANOW, settings) != 0) {
        return errno;
    }
    return EXIT_SUCCESS;
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

int test_vna(int fd) {
    const int info_size = 292;

    tcflush(fd,TCIOFLUSH);
    const char *msg = "info\r";
    if (write_command(fd, msg) < 0) {
        fprintf(stderr, "Failed to send info command\n");
        return EXIT_FAILURE;
    }

    char buffer[info_size+1];
    int num_bytes = read_exact(fd,(uint8_t*)buffer,info_size);
    buffer[num_bytes] = '\0';
    if (strstr(buffer,"NanoVNA-H"))
        return EXIT_SUCCESS;
    else
        return EXIT_FAILURE;
}