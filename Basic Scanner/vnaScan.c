#define _DEFAULT_SOURCE

// C headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>

// Linux headers
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

volatile sig_atomic_t fatal_error_in_progress = 0;
int serial_port_global = 0;
struct termios* initial_port_settings_global = NULL;

void close_and_reset(int serial_port, struct termios* initial_tty) {
    //printf("close and reseting");
    fflush(stdout);
    if (tcsetattr(serial_port, TCSANOW, initial_tty) != 0) { // restore settings
        printf("Error %i from tcsetattr, restoring: %s\n", errno, strerror(errno));
    }
    free(initial_tty);
    initial_port_settings_global = NULL;
    initial_tty = NULL;
    close(serial_port);
}

void fatal_error_signal(int sig) {
    // avoid infinite recursion
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

struct complex {
    float re;
    float im;
};

struct datapoint {
    uint32_t frequency;
    struct complex s11;
    struct complex s21;
};

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

int main() {

    if (signal(SIGINT, fatal_error_signal) == SIG_ERR) {
        fprintf(stderr, "An error occurred while setting a signal handler.\n");
        return EXIT_FAILURE;
    }

    int serial_port = open("/dev/ttyACM0", O_RDWR);
    if (serial_port < 0) {
        printf("Error %i from open: %s\n", errno, strerror(errno));
        fflush(stdout);
    }

    struct termios* initial_tty = init_serial_settings(serial_port);
    serial_port_global = serial_port;

    checkConnection(serial_port);

    unsigned char msg[] = "scan 50000000 900000000 101 135\r";
    write(serial_port, msg, sizeof(msg));

    int numBytes;
    uint16_t details[2], actual_details[2] = {135, 101};
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

    struct datapoint data[points];

    char flag = 'x';
    for (int i = 0; i < points; i++) {
        numBytes = read(serial_port, &data[i], sizeof(struct datapoint));
        if (numBytes < 0) {
            printf("Error reading: %s", strerror(errno));
            close_and_reset(serial_port, initial_tty);
            return 1;
        }
        if (numBytes == 20) {flag = 'y';}
        else {flag = 'x';}
        printf("(%d, %c) %u Hz: S11=%f+%fj, S21=%f+%fj\n", i, flag, data[i].frequency, data[i].s11.re, data[i].s11.im, data[i].s21.re, data[i].s21.im);
    }

    close_and_reset(serial_port, initial_tty);
    initial_tty = NULL;

    return 0;
}