#include "VnaCommunication.h"

int num_vnas = 0;
char **ports = NULL;

int open_serial(const char *port) {
    int fd = open(port, O_RDWR | O_NOCTTY);
    
    if (fd < 0) {
        fprintf(stderr, "Error opening serial port %s: %s\n", port, strerror(errno));
        return -1;
    }
    return fd;
}

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

int restore_serial(int fd, const struct termios *settings) {
    if (tcsetattr(fd, TCSANOW, settings) != 0) {
        return errno;
    }
    return EXIT_SUCCESS;
}

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

int in_vna_list(const char* vna_path) {
    for (int i = 0; i < num_vnas; i++) {
        if (strcmp(vna_path,ports[i]) == 0)
            return 1;
    }
    return 0;
}

int add_vna(char* vna_path) {

    if (num_vnas >= MAXIMUM_VNA_PORTS)
        return 1;

    int path_len = strlen(vna_path);
    if (path_len > MAXIMUM_VNA_PATH_LENGTH)
        return 2;

    if (in_vna_list(vna_path))
        return 3;
    
    int fd = open_serial(vna_path);
    if (fd < 0)
        return -1;
    
    struct termios initial_tty;
    if (configure_serial(fd, &initial_tty) != EXIT_SUCCESS) {
        close(fd);
        return -1;
    }

    if (test_vna(fd) != EXIT_SUCCESS) {
        restore_serial(fd,&initial_tty);
        close(fd);
        return 4;
    }
    
    ports[num_vnas] = malloc(sizeof(char)*path_len);
    if (!ports[num_vnas]) {
        restore_serial(fd,&initial_tty);
        close(fd);
        return -1;
    }
    strncpy(ports[num_vnas],vna_path,path_len);
    num_vnas++;

    restore_serial(fd,&initial_tty);
    close(fd);
    return EXIT_SUCCESS;
}

int find_vnas(char** paths) {
    DIR *d;
    struct dirent *dir;
    d = opendir("/dev");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name,"ttyACM")) {
                char vna_name[MAXIMUM_VNA_PATH_LENGTH];
                strncpy(vna_name,"/dev/",6);
                strncat(vna_name,dir->d_name,MAXIMUM_VNA_PATH_LENGTH-6);

                if (!in_vna_list(vna_name)) {
                    printf("%s\n",vna_name);
                }
            }
        }
        closedir(d);
    }
    return 0;
}

int initialise_port_array(const char* init_port) {

    if (ports) {
        // close and free all ports?
        // or move this to another function
    }

    ports = malloc(sizeof(char*)*MAXIMUM_VNA_PORTS);
    if (!ports) {
        // do an error thing
    }
    num_vnas = 0;

    if (init_port != NULL) {
        int port_len = strlen(init_port);

        ports[0] = malloc(sizeof(char)*port_len);
        if (!ports[0]) {
            // do an error thing
        }

        strncpy(ports[0],init_port,port_len);
        // check succeeded

        num_vnas = 1;
    }

    return 0;
}