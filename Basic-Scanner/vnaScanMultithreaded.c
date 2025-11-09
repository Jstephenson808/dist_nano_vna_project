#include "vnaScanMultithreaded.h"

void close_and_reset_all() {
    for (int i = VNA_COUNT-1; i >= 0; i--) {
        if (tcsetattr(SERIAL_PORTS[i], TCSANOW, &INITIAL_PORT_SETTINGS[i]) != 0) { // restore settings
            printf("Error %i from tcsetattr, restoring: %s\n", errno, strerror(errno));
        }
        close(SERIAL_PORTS[i]);
        VNA_COUNT--;
    }
}

void fatal_error_signal(int sig) {
    if (fatal_error_in_progress) {
        raise (sig);
    }
    fatal_error_in_progress = 1;

    close_and_reset_all();
    free(INITIAL_PORT_SETTINGS);
    free(SERIAL_PORTS);

    signal (sig, SIG_DFL);
    raise (sig);
}

struct termios init_serial_settings(int serial_port) {
    struct termios initial_tty; // keep to restore settings later
    if (tcgetattr(serial_port, &initial_tty) != 0) {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
    }
    struct termios tty = initial_tty; // copy for editing

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

    return initial_tty; // remember to restore later
}

void* scan_producer(void *arguments) {

    struct scan_producer_args *args = arguments;

    int total_scans = args->points / POINTS;
    int step = (args->stop - args->start) / total_scans;
    int current = args->start;

    int numBytes;
        
    uint16_t details[2], actual_details[2] = {MASK, POINTS};
    unsigned char short_buffer[4];
    unsigned char advance;

    while (total_scans > 0) {

        // give scan command
        char* msg_buff = malloc(sizeof(char)*50);    
        snprintf(msg_buff, sizeof(char)*50, "scan %d %f %i %i\r", current, round(current + step), POINTS, MASK);
        write(args->serial_port, msg_buff, sizeof(char)*50);
        free(msg_buff);
        msg_buff = NULL;

        // skip to binary header
        numBytes = read(args->serial_port, &short_buffer, sizeof(char)*4);
        if (numBytes < 0) {printf("Error reading: %s", strerror(errno));return NULL;}
        while (details[0] != actual_details[0] && details[1] != actual_details[1]) {
            numBytes = read(args->serial_port, &advance, sizeof(char));
            if (numBytes < 0) {printf("Error reading: %s", strerror(errno));return NULL;}
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
            numBytes = read(args->serial_port, data+i, sizeof(struct datapoint_NanoVNAH));
            if (numBytes < 0) {printf("Error reading: %s", strerror(errno));return NULL;}
            if (numBytes != 20) {printf("(%d) malformed", i);}
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
    complete = 1;
    return NULL;
}

void* scan_consumer(void *arguments) {

    struct scan_consumer_args *args = arguments;
    int total_count = 0;

    // warning: this outer while loop will cause infinite waiting with multiple consumer threads
    while (complete == 0 && (args->thread_args->count != 0)) {
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

void scan_coordinator(int num_vnas, int points, int start, int stop) {
    // initialise global variables

    SERIAL_PORTS = calloc(num_vnas, sizeof(int));
    INITIAL_PORT_SETTINGS = calloc(num_vnas, sizeof(struct termios));

    // create consumer and producer threads

    struct datapoint_NanoVNAH **buffer = malloc(sizeof(struct datapoint_NanoVNAH *)*(N+1));
    struct coordination_args thread_args = {0,0,0,PTHREAD_COND_INITIALIZER,PTHREAD_COND_INITIALIZER,PTHREAD_MUTEX_INITIALIZER};

    pthread_t consumer;
    struct scan_consumer_args consumer_args = {buffer,&thread_args};
    int error = pthread_create(&consumer, NULL, &scan_consumer, &consumer_args);
    if(error != 0){printf("Error %i from create consumer:\n", errno);return;}

    // warning: needs work done before this will work properly >1 VNA
    struct scan_producer_args arguments[num_vnas];
    pthread_t producers[num_vnas];
    for(int i = 0; i < num_vnas; i++) {
        SERIAL_PORTS[i] = open("/dev/ttyACM0", O_RDWR); // will need logic to decide port
        if (SERIAL_PORTS[i] < 0) {printf("Error %i from open: %s\n", errno, strerror(errno));}
        INITIAL_PORT_SETTINGS[i] = init_serial_settings(SERIAL_PORTS[i]);
        VNA_COUNT++;

        arguments[i].serial_port = SERIAL_PORTS[i];
        arguments[i].points = points;
        arguments[i].start = start;
        arguments[i].stop = stop;
        arguments[i].buffer = buffer;
        arguments[i].thread_args = &thread_args;

        error = pthread_create(&producers[i], NULL, &scan_producer, &arguments);
        if(error != 0){printf("Error %i from create producer: %s\n", errno, strerror(errno));return;}
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
int main() {

    // assign error handler
    if (signal(SIGINT, fatal_error_signal) == SIG_ERR) {
        fprintf(stderr, "An error occurred while setting a signal handler.\n");
        return EXIT_FAILURE;
    }

    // start timing
    struct timeval stop, start;
    gettimeofday(&start, NULL);

    // call a scan (with one nanoVNA)
    int points = 10100;
    scan_coordinator(1,points,50000000,900000000);

    // finish timing
    gettimeofday(&stop, NULL);
    printf("---\ntook %lf s\n", (double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_usec - start.tv_usec) / (double)1000000);
    printf("%lfs per point measurement \n", ((double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_usec - start.tv_usec) / (double)1000000)/(double)points);
    printf("%lfs points per second \n", ((double)points)/((double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_usec - start.tv_usec) / (double)1000000));

    return 0;
}