#include "VnaCommunication.h"
#include <glob.h>

int total_vnas = 0;
char **vna_names = NULL;
int * vna_fds = NULL;
struct termios* vna_initial_settings = NULL;

static volatile sig_atomic_t fatal_error_in_progress = 0; // For proper SIGINT handling

void fatal_error_signal(int sig) {
    if (fatal_error_in_progress) {
        raise (sig);
    }
    fatal_error_in_progress = 1;

    teardown_port_array();

    signal (sig, SIG_DFL);
    raise (sig);
}

int open_serial(const char *port, struct termios *init_tty) {
    int fd = open(port, O_RDWR | O_NOCTTY);
    if (fd < 0) {
         // 2. Dyanmic port detection (MacOS)
        #ifdef __APPLE__
        if (strstr(port, "ttyACM") != NULL) {
            // Checking for the "/dev/cy.usbmodem*" pattern
            glob_t glob_result;

            if (glob("/dev/cu.usbmodem*", 0, NULL, &glob_result) == 0) {
                int i = 0;
                while (i < glob_result.gl_pathc && fd < 0) {
                    char *candidate = glob_result.gl_pathv[i];

                    // Attempt to open the candidate port
                    fd = open(candidate, O_RDWR | O_NOCTTY);
                    i++;
                }
                globfree(&glob_result);
            }
        }
        if (fd < 0) {
            fprintf(stderr, "Error opening serial port %s: %s\n", port, strerror(errno));
            return -1;
        }
        #else
        fprintf(stderr, "Error opening serial port %s: %s\n", port, strerror(errno));
        return -1;
        #endif
    }

    if (configure_serial(fd,init_tty) != 0) {
        fprintf(stderr, "Error configuring port %s: %s\n", port, strerror(errno));
        close(fd);
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

ssize_t write_command(int vna_num, const char *cmd) {
    size_t cmd_len = strlen(cmd);
    ssize_t bytes_written = write(vna_fds[vna_num], cmd, cmd_len);
    
    if (bytes_written < 0) {
        fprintf(stderr, "Error writing to fd %d: %s\n", vna_fds[vna_num], strerror(errno));
        return -1;
    } else if (bytes_written < (ssize_t)cmd_len) {
        fprintf(stderr, "Warning: Partial write (%zd of %zu bytes) on fd %d\n", 
                bytes_written, cmd_len, vna_fds[vna_num]);
    }
    
    return bytes_written;
}

ssize_t read_exact(int vna_num, uint8_t *buffer, size_t length) {
    ssize_t bytes_read = 0;
    
    while (bytes_read < (ssize_t)length) {
        ssize_t n = read(vna_fds[vna_num], buffer + bytes_read, length - bytes_read);
        
        if (n < 0) {
            fprintf(stderr, "Error reading from fd %d: %s\n",
                     vna_fds[vna_num], strerror(errno));
            return -1;
        } else if (n == 0) {
            // Timeout or end of file
            if (bytes_read > 0) {
                fprintf(stderr, "Timeout: only read %zd of %zu bytes from fd %d\n", 
                        bytes_read, length, vna_fds[vna_num]);
            }
            return bytes_read;
        }
        
        bytes_read += n;
    }
    
    return bytes_read;
}

#define INFO_SIZE 292

#define INFO_SIZE 292

int test_vna(int vna_num) {
    tcflush(vna_fds[vna_num],TCIOFLUSH);
    const char *msg = "info\r";
    if (write_command(vna_num, msg) < 0) {
        fprintf(stderr, "Failed to send info command\n");
        return EXIT_FAILURE;
    }

    char buffer[INFO_SIZE+1];
    int num_bytes = read_exact(vna_num,(uint8_t*)buffer,INFO_SIZE);
    buffer[num_bytes] = '\0';
    if (strstr(buffer,"NanoVNA-H"))
        return EXIT_SUCCESS;
    else
        return EXIT_FAILURE;
}

int get_vna_count() {
    return total_vnas;
}

int in_vna_list(const char* vna_path) {
    for (int i = 0; i < total_vnas; i++) {
        if (strcmp(vna_path,vna_names[i]) == 0)
            return 1;
    }
    return 0;
}

int add_vna(char* vna_path) {
    if (total_vnas >= MAXIMUM_VNA_PORTS)
        return 1;
    int path_len = strlen(vna_path);
    if (path_len > MAXIMUM_VNA_PATH_LENGTH)
        return 2;

    if (in_vna_list(vna_path))
        return 3;
    
    int fd = open_serial(vna_path,&vna_initial_settings[total_vnas]);
    if (fd < 0)
        return -1;
    vna_fds[total_vnas] = fd;

    if (test_vna(total_vnas) != EXIT_SUCCESS) {
        restore_serial(fd,&vna_initial_settings[total_vnas]);
        close(fd);
        return 4;
    }
    
    vna_names[total_vnas] = calloc(sizeof(char),MAXIMUM_VNA_PATH_LENGTH);
    if (!vna_names[total_vnas]) {
        restore_serial(fd,&vna_initial_settings[total_vnas]);
        close(fd);
        return -1;
    }
    strncpy(vna_names[total_vnas],vna_path,path_len);
    total_vnas++;

    return EXIT_SUCCESS;
}

int remove_vna_name(char* vna_path) {
    int vna_num = -1;

    int i = 0;
    while (i < total_vnas && vna_num < 0) {
        if (strcmp(vna_path,vna_names[i]) == 0) {
            vna_num = i;
        }
        i++;
    }
    if (vna_num < 0) {
        return EXIT_FAILURE;
    }

    if (restore_serial(vna_fds[vna_num],&vna_initial_settings[vna_num]) != 0 && !fatal_error_in_progress) {
        fprintf(stderr, "Error %i restoring settings on port %d: %s\n", errno, vna_num, strerror(errno));
    }
    if (close(vna_fds[vna_num]) != 0 && !fatal_error_in_progress) {
        fprintf(stderr, "Error %i closing port %d: %s\n", errno, vna_num, strerror(errno));
    }

    if (vna_num != total_vnas-1) {
        strncpy(vna_names[vna_num],vna_names[total_vnas-1],MAXIMUM_VNA_PATH_LENGTH);
        vna_fds[vna_num] = vna_fds[total_vnas-1];
        vna_initial_settings[vna_num] = vna_initial_settings[total_vnas-1];
    }

    free(vna_names[total_vnas-1]);
    vna_names[total_vnas-1] = NULL;
    total_vnas--;

    return EXIT_SUCCESS;
}

int remove_vna_number(int vna_num) {

    if (vna_num < 0 || vna_num > total_vnas) {
        return EXIT_FAILURE;
    }

    if (restore_serial(vna_fds[vna_num],&vna_initial_settings[vna_num]) != 0 && !fatal_error_in_progress) {
        fprintf(stderr, "Error %i restoring settings on port %d: %s\n", errno, vna_num, strerror(errno));
    }
    if (close(vna_fds[vna_num]) != 0 && !fatal_error_in_progress) {
        fprintf(stderr, "Error %i closing port %d: %s\n", errno, vna_num, strerror(errno));
    }

    if (vna_num != total_vnas-1) {
        strncpy(vna_names[vna_num],vna_names[total_vnas-1],MAXIMUM_VNA_PATH_LENGTH);
        vna_fds[vna_num] = vna_fds[total_vnas-1];
        vna_initial_settings[vna_num] = vna_initial_settings[total_vnas-1];
    }

    free(vna_names[total_vnas-1]);
    vna_names[total_vnas-1] = NULL;
    total_vnas--;

    return EXIT_SUCCESS;
}

int find_vnas(char** paths, const char* search_dir) {
    DIR *d;
    struct dirent *dir;
    d = opendir(search_dir);
    if (!d)
        return -1;

    int count = 0;
    while ((dir = readdir(d)) != NULL) {
        if (strstr(dir->d_name,"ttyACM")) {
            char vna_name[MAXIMUM_VNA_PATH_LENGTH];
            strncpy(vna_name,"/dev/",6);
            strncat(vna_name,dir->d_name,MAXIMUM_VNA_PATH_LENGTH-6);

            if (!in_vna_list(vna_name) && count < MAXIMUM_VNA_PORTS) {
                paths[count] = NULL;
                paths[count] = malloc(sizeof(char) * MAXIMUM_VNA_PATH_LENGTH);
                if (paths[count] == NULL) {
                    fprintf(stderr,"failed to allocate memory\n");
                    return -1;
                }
                strncpy(paths[count],vna_name,MAXIMUM_VNA_PATH_LENGTH);
                count++;
            }
        }
    }
    closedir(d);
    return count;
}

int add_all_vnas() {
    char** paths = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);
    int found = find_vnas(paths,"/dev");
    int added = 0;
    for (int i = 0; i < found; i++) {
        int err = add_vna(paths[i]);
        if (err != 0)
            err = add_vna(paths[i]);
        if (err == 0)
            added++;
    }
    return added;
}

void vna_id() {
    char* buffer = calloc(sizeof(char),8);
    for (int i = 0; i < total_vnas; i++) {
        write_command(i,"version\r");
        read_exact(i,(uint8_t *)buffer,7);
        fprintf(stdout,"    %d. %s NanoVNA-H version %s\n",i,vna_names[i],buffer);
    }
}

void vna_ping() {
    for (int i = 0; i < total_vnas; i++) {
        if (test_vna(i) == 0) {
            fprintf(stdout,"    %s says pong\n",vna_names[i]);
        }
        else {
            fprintf(stdout,"    failed to ping %s\n",vna_names[i]);
        }
    }
}

int vna_reset(const char* vna_port) {
    return EXIT_FAILURE;
}

void vna_status() {
    // doesn't do anything until sweeps etc.
}

int initialise_port_array() {

    // assign error handler
    if (signal(SIGINT, fatal_error_signal) == SIG_ERR) {
        fprintf(stderr, "An error occurred while setting a signal handler.\n");
        return EXIT_FAILURE;
    }

    if (vna_names) {
        fprintf(stderr,"port array already initialised, skipping\n");
        return EXIT_SUCCESS;
    }

    vna_names = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);
    vna_fds = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    vna_initial_settings = calloc(sizeof(struct termios),MAXIMUM_VNA_PORTS);
    if (!vna_names || !vna_fds || !vna_initial_settings) {
        fprintf(stderr,"failed to allocate memory for port arrays\n");
        if (vna_names) {free(vna_names);vna_names=NULL;}
        if (vna_fds) {free(vna_fds);vna_fds=NULL;}
        if (vna_initial_settings) {free(vna_initial_settings);vna_initial_settings=NULL;}
        return EXIT_FAILURE;
    }
    total_vnas = 0;

    return EXIT_SUCCESS;
}

void teardown_port_array() {
    while (total_vnas > 0) {
        int i = total_vnas-1;
        remove_vna_number(i);
    }

    free(vna_initial_settings);
    vna_initial_settings = NULL;
    free(vna_names);
    vna_names = NULL;
    free(vna_fds);
    vna_fds = NULL;
}