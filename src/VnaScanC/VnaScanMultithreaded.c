#include "VnaScanMultithreaded.h"
#include <glob.h>

/**
 * Declaring global variables (for error handling and port consistency)
 * 
 * @SERIAL_PORTS is a pointer to an array of all serial port connections
 * @INITIAL_PORT_SETTINGS is a pointer to an array of all serial port's initial settings
 * @VNA_COUNT is the number of VNAs currently connected
 */
int vna_count = 0;
int POINTS = 101;

struct timeval program_start_time;

/**
 * Finds the binary header in the serial stream
 * Scans byte-by-byte looking for the header pattern (mask + points)
 * 
 * @param fd The file descriptor of the serial port
 * @param expected_mask The expected mask value (e.g., 135)
 * @param expected_points The expected points value (e.g., 101)
 * @return 1 if header found, 0 if timeout/not found, -1 on error
 */
int find_binary_header(int fd, struct nanovna_raw_datapoint* first_point, uint16_t expected_mask, uint16_t expected_points) {
    int max_bytes = 500;  // Maximum bytes to scan before giving up
    int dp_size = (unsigned int)sizeof(struct nanovna_raw_datapoint); // Amount of bytes that should be pulled at a time
    uint8_t bytes[sizeof(struct nanovna_raw_datapoint)];
    
    // Read initial 4 bytes
    if (read_exact(fd, bytes, dp_size) != dp_size) {
        fprintf(stderr, "Failed to read initial header bytes\n");
        return EXIT_FAILURE;
    }
    uint8_t window[4] = {bytes[0],bytes[1],bytes[2],bytes[3]};
    
    // Check if we already have the header
    uint16_t mask = window[0] | (window[1] << 8);
    uint16_t points = window[2] | (window[3] << 8);
    int found = (mask == expected_mask && points == expected_points) ? 1 : 0;
    
    int count = 4;
    int i = 4;
    while (!found) {
        if (count > max_bytes) {
            fprintf(stderr, "Binary header not found after %d bytes\n", max_bytes);
            return EXIT_FAILURE;
        }

        // read new data into bytes
        int err = read_exact(fd, bytes, dp_size);
        if (err < 0) {
            fprintf(stderr, "Error reading header byte: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        else if (err < dp_size) {
            fprintf(stderr, "Timeout waiting for binary header\n");
            return EXIT_FAILURE;
        }
        i = 0;

        while (i < dp_size && !found) {
            // Shift window
            window[0] = window[1];
            window[1] = window[2];
            window[2] = window[3];
            window[3] = bytes[i];
            
            // Update mask and points
            mask = window[0] | (window[1] << 8);
            points = window[2] | (window[3] << 8);
            if (mask == expected_mask && points == expected_points)
                found = 1;
                        
            i++;
        }
        count+=dp_size;
    }
    
    // Pull the rest of the first datapoint
    uint8_t remainder[i];
    if (read_exact(fd, remainder, i) != i) {
        fprintf(stderr, "Failed to finish reading first data point\n");
        return EXIT_FAILURE;
    }

    int mid = dp_size - i;
    for (int j = 0; j < mid; j++)
        bytes[j] = bytes[j+i];

    for (int j = 0; j < i; j++)
        bytes[mid+j] = remainder[j];

    memcpy(first_point,bytes,dp_size);
    return EXIT_SUCCESS;
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
        if (buffer->complete >= vna_count) {
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

    // Create struct for data points
    struct datapoint_nanoVNA_H *data = malloc(sizeof(struct datapoint_nanoVNA_H));
    if (!data) {
        fprintf(stderr, "Failed to allocate memory for data points\n");
        return NULL;
    }
    data->point = malloc(sizeof(struct nanovna_raw_datapoint) * POINTS);
    if (!data->point) {
        fprintf(stderr, "Failed to allocate memory for raw data points\n");
        free(data);
        return NULL;
    }

    // Find binary header and read first point
    int header_found = find_binary_header(port, &data->point[0], MASK, POINTS);
    if (header_found != EXIT_SUCCESS) {
        fprintf(stderr, "Failed to find binary header\n");
        free(data->point);
        free(data);
        return NULL;
    }

    // Receive data points
    for (int i = 1; i < POINTS; i++) {
        // Read raw data (20 bytes from NanoVNA)
        ssize_t bytes_read = read_exact(port, 
                                        (uint8_t*)&data->point[i], 
                                        sizeof(struct nanovna_raw_datapoint));
        
        if (bytes_read != sizeof(struct nanovna_raw_datapoint)) {
            fprintf(stderr, "Error reading data point %d: got %zd bytes, expected %zu\n", 
                    i, bytes_read, sizeof(struct nanovna_raw_datapoint));
            free(data->point);
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

    for (int sweep = 0; sweep < args->nbr_sweeps; sweep++) {
        if (args->nbr_sweeps > 1) {
            printf("[Producer] Starting sweep %d/%d\n", sweep + 1, args->nbr_sweeps);
        }

        int current = args->start;
        int step = (int)round(args->stop - args->start) / ((args->nbr_scans*POINTS)-1);
        for (int scan = 0; scan < args->nbr_scans; scan++) {
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
    while (args->bfr->complete == 0) {
        sweep++;
        if (args->nbr_sweeps > 1) {
            printf("[Producer] Starting sweep %d\n", sweep + 1);
        }
        int total_scans = args->nbr_scans;
        int step = (args->stop - args->start) / total_scans;
        int current = args->start;
        while (total_scans > 0) {
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
    args->b->complete = vna_count;
    printf("---\ntimer done\n---\n");
    return NULL;
}

void* scan_consumer(void *arguments) {

    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;
    FILE *f = args->touchstone_file;
    printf("ID Label VNA TimeSent TimeRecv Freq SParam Format Value\n");

    while (args->bfr->complete < vna_count || (args->bfr->count != 0)) {

        struct datapoint_nanoVNA_H *data = take_buff(args->bfr);
        if (!data) {
            // take_buff has returned nothing as there was nothing left to take
            return NULL;
        }

        double send_secs = ((double)(data->send_time.tv_sec - program_start_time.tv_sec) + 
                            (double)(data->send_time.tv_usec - program_start_time.tv_usec) / 1e6);
        double recv_secs = ((double)(data->receive_time.tv_sec - program_start_time.tv_sec) + 
                            (double)(data->receive_time.tv_usec - program_start_time.tv_usec) / 1e6);

        for (int i = 0; i < POINTS; i++) {
            struct nanovna_raw_datapoint *p = &data->point[i];
            
            // Console output
            // Row 1: S11 Real
            printf("%s %s %d %.6f %.6f %u S11 REAL %.10e\n",
                args->id_string, args->label, data->vna_id, send_secs, recv_secs, p->frequency, p->s11.re);
            // Row 2: S11 Imaginary
            printf("%s %s %d %.6f %.6f %u S11 IMG %.10e\n",
                args->id_string, args->label, data->vna_id, send_secs, recv_secs, p->frequency, p->s11.im);
            // Row 3: S21 Real
            printf("%s %s %d %.6f %.6f %u S21 REAL %.10e\n",
                args->id_string, args->label, data->vna_id, send_secs, recv_secs, p->frequency, p->s21.re);
            // Row 4: S21 Imaginary
            printf("%s %s %d %.6f %.6f %u S21 IMG %.10e\n",
                args->id_string, args->label, data->vna_id, send_secs, recv_secs, p->frequency, p->s21.im);
            
            // Touchstone File Output
            if (f) {
                fprintf(f, "%u %.10e %.10e %.10e %.10e 0 0 0 0\n",
                    p->frequency, p->s11.re, p->s11.im, p->s21.re, p->s21.im);
            }
        }

        free(data->point);
        free(data);
    }
    return NULL;
}

void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, int pps, int *vna_fds, const char *user_label){
    int error;

    if (vna_fds == NULL) {
        fprintf(stderr, "No VNAs passed\n");
        return;
    }

    // Initialise global variables
    vna_count = num_vnas;
    POINTS = pps;

    gettimeofday(&program_start_time, NULL);

    // Create Touchstone file
    char filename[128];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(filename, sizeof(filename), "vna_scan_at_%Y-%m-%d_%H-%M-%S.s2p", tm_info);

    // ID String
    char id_string[64];
    strftime(id_string, sizeof(id_string), "%Y%m%d_%H%M%S", tm_info);

    FILE *touchstone_file = fopen(filename, "w");
    if (!touchstone_file) {
        fprintf(stderr, "Warning: Failed to open %s for writing. Scan will continue without saving.\n", filename);
    } else {
        printf("Saving data to: %s\n", filename);
        // Write standard Touchstone Header
        fprintf(touchstone_file, "! Touchstone file generated from multi-VNA scan\n");
        fprintf(touchstone_file, "! One file containing all VNAS interleaved\n");
        fprintf(touchstone_file, "# Hz S RI R 50\n");
    }

    // Create consumer and producer threads
    BoundedBuffer *bounded_buffer = malloc(sizeof(BoundedBuffer));
    if (!bounded_buffer) {
        fprintf(stderr, "Failed to allocate memory for bounded buffer construct\n");
        return;
    }
    error = create_bounded_buffer(bounded_buffer);
    if (error != 0) {
        fprintf(stderr, "Failed to create bounded buffer\n");
        free(bounded_buffer);
        bounded_buffer = NULL;
        return;
    }
    
    struct scan_producer_args arguments[num_vnas];
    pthread_t producers[num_vnas];
    for(int i = 0; i < num_vnas; i++) {
        arguments[i].vna_id = i;
        arguments[i].serial_port = vna_fds[i];
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
    struct scan_consumer_args consumer_args = {
        bounded_buffer, 
        touchstone_file,
        id_string,
        (char*)user_label
    };
    error = pthread_create(&consumer, NULL, &scan_consumer, &consumer_args);
    if(error != 0){
        fprintf(stderr, "Error %i creating consumer thread: %s\n", errno, strerror(errno));
        destroy_bounded_buffer(bounded_buffer);
        bounded_buffer=NULL;
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
        if(error != 0)
            printf("Error %i from join timer:\n", errno);
    }

    // wait for threads to finish

    for(int i = 0; i < num_vnas; i++) {
        error = pthread_join(producers[i], NULL);
        if(error != 0)
            printf("Error %i from join producer:\n", errno);
    }

    error = pthread_join(consumer,NULL);
    if(error != 0)
        printf("Error %i from join consumer:\n", errno);

    // close touchstone file
    if (touchstone_file) {
        fclose(touchstone_file);
    }

    // finish up
    destroy_bounded_buffer(bounded_buffer);
    bounded_buffer = NULL;
    return;
}