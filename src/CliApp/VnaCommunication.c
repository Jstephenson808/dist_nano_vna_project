#include "VnaCommunication.h"
#include <glob.h>
#include <pthread.h>

// Extern from VnaScanMultithreaded
extern int get_ongoing_scan_count(void);

/**
 * Mutex to protect global VNA state arrays (vna_fds, vna_names, etc.)
 */
static pthread_mutex_t vna_state_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Current number of connected VNAs
 */
int total_vnas = 0;

/**
 * Pointers to the paths of all currently connected VNAs
 * path length < MAXIMUM_VNA_PATH_LENGTH
 * indexed by vna_id
 * NULL if unnocupied, must be malloced and freed
 */
char **vna_names = NULL;

/**
 * The file descriptors of all currently connected VNAs
 * indexed by vna_id
 * -1 if unnocupied, >=0 if occupied.
 */
int * vna_fds = NULL;

/**
 * The initial settings of all currently connected VNAs
 * indexed by vna_id
 * May be any value when unnocupied, must be written over.
 */
struct termios* vna_initial_settings = NULL;

/**
 * For SIGINT handling, ensures signal handler cannot
 * devolve into endless recursion.
 */
static volatile sig_atomic_t fatal_error_in_progress = 0;

/**
 * Helper to safely retrieve a file descriptor.
 * Returns -1 if VNA is not connected or fd array is invalid.
 */
static int get_fd_safe(int vna_num) {
    if (vna_num < 0 || vna_num >= MAXIMUM_VNA_PORTS) return -1;
    pthread_mutex_lock(&vna_state_lock);
    int fd = (vna_fds != NULL) ? vna_fds[vna_num] : -1;
    pthread_mutex_unlock(&vna_state_lock);
    return fd;
}

void fatal_error_signal(int sig) {
    if (fatal_error_in_progress) {
        raise (sig);
    }
    fatal_error_in_progress = 1;

    // We still try to teardown on fatal error, but it may fail if scans are active.
    // In a real CLI, we might want a 'force' teardown for SIGINT, 
    // but here we follow the mandated lifecycle.
    if (get_ongoing_scan_count() == 0) {
        teardown_port_array();
    } else {
        fprintf(stderr, "\nFatal error: Active scans preventing clean port restoration.\n");
    }

    signal (sig, SIG_DFL);
    raise (sig);
}

int open_serial(const char *port, struct termios *init_tty) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK); // Open non-blocking for safety
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
                    fd = open(candidate, O_RDWR | O_NOCTTY | O_NONBLOCK);
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

    // Set to blocking mode for standard operations after open
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

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
    int fd = get_fd_safe(vna_num);
    if (fd < 0) return -1;

    size_t cmd_len = strlen(cmd);
    size_t total_written = 0;
    
    while (total_written < cmd_len) {
        ssize_t bytes_written = write(fd, cmd + total_written, cmd_len - total_written);
        if (bytes_written < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "Error writing to fd %d: %s\n", fd, strerror(errno));
            return -1;
        }
        total_written += bytes_written;
    }
    
    return (ssize_t)total_written;
}

ssize_t read_exact(int vna_num, uint8_t *buffer, size_t length) {
    ssize_t total_read = 0;
    int retries = 0;
    const int max_retries = 3; // Retry on timeout if some data was already read

    while (total_read < (ssize_t)length) {
        int fd = get_fd_safe(vna_num);
        if (fd < 0) return -1;

        ssize_t n = read(fd, buffer + total_read, length - total_read);
        
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            fprintf(stderr, "Error reading from fd %d: %s\n", fd, strerror(errno));
            return -1;
        } else if (n == 0) {
            // Timeout reached (due to VMIN=0, VTIME=10)
            if (total_read > 0 && retries < max_retries) {
                retries++;
                continue; 
            }
            // Fatal timeout or EOF
            return total_read; 
        }
        
        total_read += n;
        retries = 0; // Reset retries on successful read
    }
    
    return total_read;
}

#define INFO_SIZE 292

int test_vna(int vna_num) {
    int fd = get_fd_safe(vna_num);
    if (fd < 0) return EXIT_FAILURE;

    tcflush(fd, TCIOFLUSH);
    const char *msg = "info\r";
    if (write_command(vna_num, msg) < 0) {
        fprintf(stderr, "Failed to send info command\n");
        return EXIT_FAILURE;
    }

    char buffer[INFO_SIZE+1];
    int num_bytes = read_exact(vna_num,(uint8_t*)buffer,INFO_SIZE);
    if (num_bytes < INFO_SIZE) return EXIT_FAILURE;

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
    pthread_mutex_lock(&vna_state_lock);
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        if (vna_names && vna_names[i] && strcmp(vna_path,vna_names[i]) == 0) {
            pthread_mutex_unlock(&vna_state_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&vna_state_lock);
    return 0;
}

bool is_connected(int vna_id) {
    return (get_fd_safe(vna_id) >= 0);
}

int get_connected_vnas(int* vna_list) {
    int count = 0;
    pthread_mutex_lock(&vna_state_lock);
    if (vna_fds) {
        for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
            if (vna_fds[i] >= 0) {
                vna_list[count++] = i;
            }
        }
    }
    pthread_mutex_unlock(&vna_state_lock);
    return count;
}

int add_vna(char* vna_path) {
    if (total_vnas >= MAXIMUM_VNA_PORTS)
        return 1;
    int path_len = strlen(vna_path);
    if (path_len > MAXIMUM_VNA_PATH_LENGTH)
        return 2;

    if (in_vna_list(vna_path))
        return 3;
    
    struct termios temp_settings;
    int fd = open_serial(vna_path, &temp_settings);
    if (fd < 0)
        return -1;

    pthread_mutex_lock(&vna_state_lock);
    int vna_id = -1;
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        if (vna_fds[i] < 0) {
            vna_id = i;
            break;
        }
    }

    if (vna_id < 0) {
        pthread_mutex_unlock(&vna_state_lock);
        close(fd);
        return -1;
    }

    vna_fds[vna_id] = fd;
    vna_initial_settings[vna_id] = temp_settings;
    pthread_mutex_unlock(&vna_state_lock);

    if (test_vna(vna_id) != EXIT_SUCCESS) {
        pthread_mutex_lock(&vna_state_lock);
        restore_serial(fd,&vna_initial_settings[vna_id]);
        close(fd);
        vna_fds[vna_id] = -1;
        pthread_mutex_unlock(&vna_state_lock);
        return 4;
    }
    
    char *name_copy = calloc(sizeof(char), MAXIMUM_VNA_PATH_LENGTH);
    if (!name_copy) {
        pthread_mutex_lock(&vna_state_lock);
        restore_serial(fd,&vna_initial_settings[vna_id]);
        close(fd);
        vna_fds[vna_id] = -1;
        pthread_mutex_unlock(&vna_state_lock);
        return -1;
    }
    strncpy(name_copy, vna_path, MAXIMUM_VNA_PATH_LENGTH - 1);
    
    pthread_mutex_lock(&vna_state_lock);
    vna_names[vna_id] = name_copy;
    total_vnas++;
    pthread_mutex_unlock(&vna_state_lock);

    return EXIT_SUCCESS;
}

int remove_vna_name(char* vna_path) {
    if (get_ongoing_scan_count() > 0) {
        fprintf(stderr, "Cannot remove VNA while scans are active.\n");
        return EXIT_FAILURE;
    }

    int vna_num = -1;
    pthread_mutex_lock(&vna_state_lock);
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        if (vna_names && vna_names[i] && strcmp(vna_path, vna_names[i]) == 0) {
            vna_num = i;
            break;
        }
    }
    pthread_mutex_unlock(&vna_state_lock);

    if (vna_num < 0) return EXIT_FAILURE;
    return remove_vna_number(vna_num);
}

int remove_vna_number(int vna_num) {
    if (get_ongoing_scan_count() > 0) {
        fprintf(stderr, "Cannot remove VNA while scans are active.\n");
        return EXIT_FAILURE;
    }

    if (vna_num < 0 || vna_num >= MAXIMUM_VNA_PORTS) return EXIT_FAILURE;

    pthread_mutex_lock(&vna_state_lock);
    if (!vna_fds || vna_fds[vna_num] < 0) {
        pthread_mutex_unlock(&vna_state_lock);
        return EXIT_FAILURE;
    }

    int fd = vna_fds[vna_num];
    if (restore_serial(fd, &vna_initial_settings[vna_num]) != 0 && !fatal_error_in_progress) {
        fprintf(stderr, "Error restoring settings on port %d\n", vna_num);
    }
    close(fd);

    vna_fds[vna_num] = -1;
    if (vna_names[vna_num]) {
        free(vna_names[vna_num]);
        vna_names[vna_num] = NULL;
    }
    total_vnas--;
    pthread_mutex_unlock(&vna_state_lock);

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
            snprintf(vna_name, sizeof(vna_name), "/dev/%s", dir->d_name);

            if (!in_vna_list(vna_name) && count < MAXIMUM_VNA_PORTS) {
                paths[count] = malloc(sizeof(char) * MAXIMUM_VNA_PATH_LENGTH);
                if (paths[count] == NULL) {
                    fprintf(stderr,"failed to allocate memory\n");
                    closedir(d);
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
        if (add_vna(paths[i]) == 0) added++;
        free(paths[i]);
    }
    free(paths);
    return added;
}

void vna_id() {
    char buffer[16];
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        int fd = get_fd_safe(i);
        if (fd < 0) continue;

        tcflush(fd, TCIOFLUSH);
        write_command(i,"version\r");
        if (read_exact(i,(uint8_t *)buffer, 7) == 7) {
            buffer[7] = '\0';
            fprintf(stdout,"    %d. %s NanoVNA-H version %s\n",i,vna_names[i],buffer);
        }
    }
}

void vna_ping() {
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        if (get_fd_safe(i) < 0) continue;
        if (test_vna(i) == 0) {
            fprintf(stdout,"    %s says pong\n",vna_names[i]);
        }
        else {
            fprintf(stdout,"    failed to ping %s\n",vna_names[i]);
        }
    }
}

void vna_reset() {
    if (get_ongoing_scan_count() > 0) {
        fprintf(stderr, "Cannot reset VNAs while scans are active.\n");
        return;
    }

    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        if (get_fd_safe(i) >= 0) {
            write_command(i,"reset\r");
        }
    }
    teardown_port_array();
    initialise_port_array();
}

void vna_status() {
    // doesn't do anything until sweeps etc.
}

void print_vnas() {
    pthread_mutex_lock(&vna_state_lock);
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        if (vna_fds && vna_fds[i] >= 0) {
            printf("    %d. %s\n", i, vna_names[i]);
        }
    }
    pthread_mutex_unlock(&vna_state_lock);
}

int initialise_port_array() {

    // assign error handler
    if (signal(SIGINT, fatal_error_signal) == SIG_ERR) {
        fprintf(stderr, "An error occurred while setting a signal handler.\n");
        return EXIT_FAILURE;
    }

    pthread_mutex_lock(&vna_state_lock);
    if (vna_names) {
        pthread_mutex_unlock(&vna_state_lock);
        return EXIT_SUCCESS;
    }

    vna_names = calloc(sizeof(char*),MAXIMUM_VNA_PORTS);
    vna_fds = malloc(sizeof(int) * MAXIMUM_VNA_PORTS);
    vna_initial_settings = calloc(sizeof(struct termios),MAXIMUM_VNA_PORTS);
    
    if (!vna_names || !vna_fds || !vna_initial_settings) {
        fprintf(stderr,"failed to allocate memory for port arrays\n");
        free(vna_names); vna_names = NULL;
        free(vna_fds); vna_fds = NULL;
        free(vna_initial_settings); vna_initial_settings = NULL;
        pthread_mutex_unlock(&vna_state_lock);
        return EXIT_FAILURE;
    }
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++)
        vna_fds[i] = -1;
    total_vnas = 0;
    pthread_mutex_unlock(&vna_state_lock);

    return EXIT_SUCCESS;
}

void teardown_port_array() {
    if (get_ongoing_scan_count() > 0) {
        fprintf(stderr, "Cannot teardown ports while scans are active.\n");
        return;
    }

    pthread_mutex_lock(&vna_state_lock);
    if (vna_fds == NULL) {
        pthread_mutex_unlock(&vna_state_lock);
        return;
    }

    // We can't use remove_vna_number here because it also locks the mutex.
    // We'll perform manual cleanup.
    for (int i = 0; i < MAXIMUM_VNA_PORTS; i++) {
        if (vna_fds[i] >= 0) {
            restore_serial(vna_fds[i], &vna_initial_settings[i]);
            close(vna_fds[i]);
            vna_fds[i] = -1;
            if (vna_names[i]) {
                free(vna_names[i]);
                vna_names[i] = NULL;
            }
        }
    }

    free(vna_initial_settings);
    vna_initial_settings = NULL;
    free(vna_names);
    vna_names = NULL;
    free(vna_fds);
    vna_fds = NULL;
    total_vnas = 0;
    pthread_mutex_unlock(&vna_state_lock);
}