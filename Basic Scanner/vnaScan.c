// needed for CRTSCTS macro
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

#define POINTS 101
#define MASK 135

//Declaring global variables (for error handling only)
volatile sig_atomic_t fatal_error_in_progress = 0;
int serial_port_global = 0;
struct termios* initial_port_settings_global = NULL;


//Declaring structs for data points
struct complex {
    float re;
    float im;
};
struct datapoint {
    uint32_t frequency;
    struct complex s11;
    struct complex s21;
};

//=============================================
// Port Restoration
//=============================================

/*
 * Closes port and restores initial settings
 */
void close_and_reset(int serial_port, struct termios* initial_tty) {
    if (tcsetattr(serial_port, TCSANOW, initial_tty) != 0) { // restore settings
        printf("Error %i from tcsetattr, restoring: %s\n", errno, strerror(errno));
    }
    free(initial_tty);
    initial_port_settings_global = NULL;
    initial_tty = NULL;
    close(serial_port);
}

/*
 * Fatal error handling. 
 * 
 * Calls close_and_reset before allowing the program to exit normally.
 * Uses fatal_error_in_progress to prevent infinite recursion.
 */
void fatal_error_signal(int sig) {
    if (fatal_error_in_progress) {
        raise (sig);
    }
    fatal_error_in_progress = 1;

    if (initial_port_settings_global != NULL) {
        close_and_reset(serial_port_global, initial_port_settings_global);
    }

    signal (sig, SIG_DFL);
    raise (sig);
}

//=============================================
// Serial Interface
//=============================================

/*
 * Amend port settings
 * 
 * Edits port settings to interact with a serial interface.
 * Flags should only be edited with bitwise operations.
 * Writes are permanent: initial settings are kept to restore on program close.
 * 
 * @serial_port should already be opened successfully
 * @return initial settings to restore. Also stored in global variable.
 */
struct termios* init_serial_settings(int serial_port) {
    struct termios *initial_tty = malloc(sizeof(struct termios)); // keep to restore settings later
    if (tcgetattr(serial_port, initial_tty) != 0) {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
    }
    struct termios tty = *initial_tty; // copy for editing

    tty.c_cflag &= ~PARENB; // no parity
    tty.c_cflag &= ~CSTOPB; // one stop bit

    tty.c_cflag &= ~CSIZE; // clear size bits
    tty.c_cflag |= CS8; // 8 bit

    tty.c_cflag &= ~CRTSCTS; // no hw flow control
    tty.c_cflag |= CLOCAL; // ignore control lines

    tty.c_cflag |= CREAD; // turn on READ

    tty.c_lflag &= ~ICANON; // disable canonical mode
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo

    tty.c_lflag &= ~ISIG; // no signal chars

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // no sw flow ctrl

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // no special handling of recieved bytes
    tty.c_oflag &= ~OPOST; // no special handling of outgoing bytes
    tty.c_oflag &= ~ONLCR; // no newline conversion

    tty.c_cc[VTIME] = 50; // timeout 5 seconds
    tty.c_cc[VMIN] = 20; // minimum recieved 20 bytes

    cfsetspeed(&tty, B115200); // baud rate 115200

    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) { // save settings
        printf("Error %i from tcsetattr, setting: %s\n", errno, strerror(errno));
    }

    initial_port_settings_global = initial_tty;
    return initial_tty; // remember to restore later
}

//=============================================
// Main Program
//=============================================

/*
 * Issues info command and prints output
 */
void checkConnection(int serial_port) {
    int numBytes;
    char buffer[32];

    unsigned char msg[] = "info\r";
    write(serial_port, msg, sizeof(msg));

    do {
        numBytes = read(serial_port,&buffer,sizeof(char)*31);
        buffer[numBytes] = '\0';
        printf("%s", (unsigned char*)buffer);
    } while (numBytes > 0 && !strstr(buffer,"ch>"));

    return;
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
int main() {

    // assign error handler

    if (signal(SIGINT, fatal_error_signal) == SIG_ERR) {
        fprintf(stderr, "An error occurred while setting a signal handler.\n");
        return EXIT_FAILURE;
    }

    // create port

    int serial_port = open("/dev/ttyACM0", O_RDWR);
    if (serial_port < 0) {
        printf("Error %i from open: %s\n", errno, strerror(errno));
        fflush(stdout);
    }

    struct termios* initial_tty = init_serial_settings(serial_port);
    serial_port_global = serial_port;
    //checkConnection(serial_port);

    // give scan command

    char* msg = malloc(sizeof(char)*50);    
    snprintf(msg, sizeof(char)*50, "scan 50000000 900000000 %i %i\r", POINTS, MASK);
    write(serial_port, msg, sizeof(char)*50);
    free(msg);

    // skip to binary header

    int numBytes;
    uint16_t details[2], actual_details[2] = {MASK, POINTS};
    unsigned char short_buffer[4];
    unsigned char advance;

    numBytes = read(serial_port, &short_buffer, sizeof(char)*4);
    while (details[0] != actual_details[0] && details[1] != actual_details[1]) {
        numBytes = read(serial_port, &advance, sizeof(char));
        for (int i = 0; i < 3; i++) {
            short_buffer[i] = short_buffer[i+1];
        }
        short_buffer[3] = advance;
        details[0] = (((short_buffer[1] << 8) & 0xFF00) | (short_buffer[0] & 0xFF));
        details[1] = (((short_buffer[3] << 8) & 0xFF00) | (short_buffer[2] & 0xFF));
    }

    printf("mask: %d, points: %d\n", details[0], details[1]);
    int points = details[1];

    // recieve output

    struct datapoint data[points];

    for (int i = 0; i < points; i++) {
        numBytes = read(serial_port, &data[i], sizeof(struct datapoint));
        if (numBytes < 0) {
            printf("Error reading: %s", strerror(errno));
            close_and_reset(serial_port, initial_tty);
            return 1;
        }
        if (numBytes != 20) {
            printf("(%d) malformed", i);
        }
    }

    for (int i = 0; i < points; i++) {
        printf("(%d) %u Hz: S11=%f+%fj, S21=%f+%fj\n", i, data[i].frequency, data[i].s11.re, data[i].s11.im, data[i].s21.re, data[i].s21.im);
    }

    close_and_reset(serial_port, initial_tty);
    initial_tty = NULL;

    return 0;
}