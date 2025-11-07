#include "vnaScanMultithreaded.h"

void close_and_reset(int serial_port, struct termios* initial_tty) {
    if (tcsetattr(serial_port, TCSANOW, initial_tty) != 0) { // restore settings
        printf("Error %i from tcsetattr, restoring: %s\n", errno, strerror(errno));
    }
    free(initial_tty);
    initial_port_settings_global = NULL;
    initial_tty = NULL;
    close(serial_port);
}

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
    tty.c_oflag &= ~OPOST; // no special handling of outgoacting as the producer to scan_consumer's consumering bytes
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

void* scan_producer(void *args) {

    struct scan_producer_args *arguments = args;

    int total_scans = arguments->points / POINTS;
    int step = (arguments->stop - arguments->start) / total_scans;
    int current = arguments->start;

    int numBytes;
        
    uint16_t details[2], actual_details[2] = {MASK, POINTS};
    unsigned char short_buffer[4];
    unsigned char advance;

    while (total_scans > 0) {

        // give scan command
        char* msg_buff = malloc(sizeof(char)*50);    
        snprintf(msg_buff, sizeof(char)*50, "scan %d %f %i %i\r", current, round(current + step), POINTS, MASK);
        write(arguments->serial_port, msg_buff, sizeof(char)*50);
        free(msg_buff);
        msg_buff = NULL;

        // skip to binary header
        numBytes = read(arguments->serial_port, &short_buffer, sizeof(char)*4);
        if (numBytes < 0) {printf("Error reading: %s", strerror(errno));close_and_reset(arguments->serial_port, initial_port_settings_global);return NULL;}
        while (details[0] != actual_details[0] && details[1] != actual_details[1]) {
            numBytes = read(arguments->serial_port, &advance, sizeof(char));
            if (numBytes < 0) {printf("Error reading: %s", strerror(errno));close_and_reset(arguments->serial_port, initial_port_settings_global);return NULL;}
            for (int i = 0; i < 3; i++) {
                short_buffer[i] = short_buffer[i+1];
            }
            short_buffer[3] = advance;
            details[0] = (((short_buffer[1] << 8) & 0xFF00) | (short_buffer[0] & 0xFF));
            details[1] = (((short_buffer[3] << 8) & 0xFF00) | (short_buffer[2] & 0xFF));
        }

        // recieve output

        struct datapoint_NanoVNAH *data = malloc(sizeof(struct datapoint_NanoVNAH) * POINTS);

        for (int i = 0; i < POINTS; i++) {
            numBytes = read(arguments->serial_port, data+i, sizeof(struct datapoint_NanoVNAH));
            if (numBytes < 0) {printf("Error reading: %s", strerror(errno));close_and_reset(arguments->serial_port, initial_port_settings_global);return NULL;}
            if (numBytes != 20) {printf("(%d) malformed", i);}
        }

        // add to buffer

        pthread_mutex_lock(&lock);
        while (count == N) {
            pthread_cond_wait(&remove_cond, &lock);
        }
        arguments->buffer[in] = data;
        in = (in+1) % N;
        count++;
        pthread_cond_signal(&fill_cond);
        pthread_mutex_unlock(&lock);

        // finish loop

        total_scans--;
        current += step;
    }
    complete = 1;
    return NULL;
}

void* scan_consumer(void *args) {

    struct datapoint_NanoVNAH **buffer = args;
    int total_count = 0;

    // warning: this outer while loop will cause infinite waiting with multiple consumer threads
    while (complete == 0 && (count != 0)) {
        pthread_mutex_lock(&lock);
        while (count == 0) {
            pthread_cond_wait(&fill_cond, &lock);
        }
        struct datapoint_NanoVNAH *data = buffer[out];
        buffer[out] = NULL;
        out = (out + 1) % N;
        count--;
        pthread_cond_signal(&remove_cond);
        pthread_mutex_unlock(&lock);

        for (int i = 0; i < POINTS; i++) {
            printf("(%d) %u Hz: S11=%f+%fj, S21=%f+%fj\n", total_count, data[i].frequency, data[i].s11.re, data[i].s11.im, data[i].s21.re, data[i].s21.im);
            total_count++;
        }

        free(data);
        data = NULL;
    }
    return NULL;
}

void scan_coordinator() {
    // TODO - implement scan_coordinator
}

/*
 * Helper function. Issues info command and prints output
 */
int checkConnection(int serial_port) {
    int numBytes;
    char buffer[32];

    unsigned char msg[] = "info\r";
    write(serial_port, msg, sizeof(msg));

    do {
        numBytes = read(serial_port,&buffer,sizeof(char)*31);
        if (numBytes < 0) {printf("Error reading: %s", strerror(errno));close_and_reset(serial_port, initial_port_settings_global);return 1;}
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
int main() {

    struct timeval stop, start;
    gettimeofday(&start, NULL);

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

    // create threads

    struct datapoint_NanoVNAH **buffer = malloc(sizeof(struct datapoint_NanoVNAH *)*(N+1));

    struct scan_producer_args arguments;
    arguments.serial_port = serial_port;
    arguments.points = 10100;
    arguments.start = 50000000;
    arguments.stop = 900000000;
    arguments.buffer = buffer;

    pthread_t producer;
    int error = pthread_create(&producer, NULL, &scan_producer, &arguments);
    if(error != 0){printf("Error %i from create producer: %s\n", errno, strerror(errno));return 1;}

    pthread_t consumer;
    error = pthread_create(&consumer, NULL, &scan_consumer,buffer);
    if(error != 0){printf("Error %i from create consumer:\n", errno);return 1;}

    error = pthread_join(producer, NULL);
    if(error != 0){printf("Error %i from join producer:\n", errno);return 1;}

    error = pthread_join(consumer,NULL);
    if(error != 0){printf("Error %i from join consumer:\n", errno);return 1;}

    // finish up
    free(buffer);
    buffer = NULL;
    close_and_reset(serial_port, initial_tty);
    initial_tty = NULL;

    gettimeofday(&stop, NULL);
    printf("---\ntook %lf s\n", (double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_usec - start.tv_usec) / (double)1000000);
    printf("%lfs per point measurement \n", ((double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_usec - start.tv_usec) / (double)1000000)/(double)arguments.points);
    printf("%lfs points per second \n", ((double)arguments.points)/((double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_usec - start.tv_usec) / (double)1000000));

    return 0;
}