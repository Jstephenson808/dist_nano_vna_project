#include "VnaScanMultithreaded.h"
#include <glob.h>

//---------------------------------------------------
// Scan state global variables (access with mutex)
//---------------------------------------------------

/**
 * current number of ongoing scans
 */
int ongoing_scans = 0;

/**
 * Array of scan states. Indexed by scan_id.
 * 
 * Values:
 * -1 = unused
 * 0  = starting or finishing
 * >0 = number of scan threads still to finish (NUM_SCANS sweeps)
 *      scan active, value = number of VNAs (TIME / ONGOING sweeps)
 */
int* scan_states = NULL;

/**
 * Array of pointers to main threads of scans. Indexed by scan_id
 */
pthread_t* scan_threads = NULL;

/**
 * Mutex to make state variables thread safe.
 */
pthread_mutex_t scan_state_lock = PTHREAD_MUTEX_INITIALIZER;

//----------------------------------------
// Bounded Buffer Logic
//----------------------------------------

int create_bounded_buffer(struct bounded_buffer *bb, int pps) {
    struct datapoint_nanoVNA_H **buffer = malloc(sizeof(struct datapoint_nanoVNA_H *)*N);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer memory\n");
        return EXIT_FAILURE;
    }
    *bb = (struct bounded_buffer){buffer,0,0,0,pps,0,PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,PTHREAD_COND_INITIALIZER};
    return EXIT_SUCCESS;
}

void destroy_bounded_buffer(struct bounded_buffer *buffer) {
    free(buffer->buffer);
    buffer->buffer = NULL;
    free(buffer);
    buffer = NULL;
}

void add_buff(struct bounded_buffer *buffer, struct datapoint_nanoVNA_H *data) {
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

struct datapoint_nanoVNA_H* take_buff(struct bounded_buffer *buffer) {
    pthread_mutex_lock(&buffer->lock);
    while (buffer->count == 0) {
        if (buffer->complete == true) {
            pthread_mutex_unlock(&buffer->lock);
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

//----------------------------------------
// Pulling Data Logic
//----------------------------------------

int find_binary_header(int vna_id, struct nanovna_raw_datapoint* first_point, uint16_t expected_mask, uint16_t expected_points) {
    int max_bytes = 500;  // Maximum bytes to scan before giving up
    int dp_size = (unsigned int)sizeof(struct nanovna_raw_datapoint); // Amount of bytes that should be pulled at a time
    uint8_t bytes[sizeof(struct nanovna_raw_datapoint)];
    
    // Read initial 4 bytes
    if (read_exact(vna_id, bytes, dp_size) != dp_size) {
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
        int err = read_exact(vna_id, bytes, dp_size);
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
    if (read_exact(vna_id, remainder, i) != i) {
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

struct datapoint_nanoVNA_H* pull_scan(int vna_id, int start, int stop, int pps) {
    struct timeval send_time, receive_time;
    gettimeofday(&send_time, NULL);

    // Send scan command
    char msg_buff[50];
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, stop, pps, MASK);
    if (write_command(vna_id, msg_buff) < 0) {
        fprintf(stderr, "Failed to send scan command\n");
        return NULL;
    }

    // Create struct for data points
    struct datapoint_nanoVNA_H *data = malloc(sizeof(struct datapoint_nanoVNA_H));
    if (!data) {
        fprintf(stderr, "Failed to allocate memory for data points\n");
        return NULL;
    }
    data->point = malloc(sizeof(struct nanovna_raw_datapoint) * pps);
    if (!data->point) {
        fprintf(stderr, "Failed to allocate memory for raw data points\n");
        free(data);
        return NULL;
    }

    // Find binary header and read first point
    int header_found = find_binary_header(vna_id, &data->point[0], MASK, pps);
    if (header_found != EXIT_SUCCESS) {
        fprintf(stderr, "Failed to find binary header\n");
        free(data->point);
        free(data);
        return NULL;
    }

    // Receive data points
    for (int i = 1; i < pps; i++) {
        // Read raw data (20 bytes from NanoVNA)
        ssize_t bytes_read = read_exact(vna_id, 
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
    data->vna_id = vna_id;
    // Set Timestamps
    gettimeofday(&receive_time, NULL);
    data->send_time = send_time;
    data->receive_time = receive_time;
    return data;
}

//----------------------------------------
// Producer/Consumer Thread Logic
//----------------------------------------

void* scan_producer(void *arguments) {

    struct scan_producer_args *args = (struct scan_producer_args*)arguments;
    int pps = args->bfr->pps;

    for (int sweep = 0; sweep < args->nbr_sweeps; sweep++) {
        if (args->nbr_sweeps > 1) {
            printf("[Producer] Starting sweep %d/%d\n", sweep + 1, args->nbr_sweeps);
        }

        int current = args->start;
        int step = (int)round(args->stop - args->start) / ((args->nbr_scans*pps)-1);
        for (int scan = 0; scan < args->nbr_scans; scan++) {
            struct datapoint_nanoVNA_H *data = pull_scan(args->vna_id,current,
                                                        current + step*(pps-1), pps);
            // add to buffer
            if (data)
                add_buff(args->bfr,data);

            current += step*args->bfr->pps;
        }
    }
    pthread_mutex_lock(&scan_state_lock);
    if (--scan_states[args->scan_id] <= 0)
        args->bfr->complete = true;
    pthread_mutex_unlock(&scan_state_lock);
    return NULL;
}

void* sweep_producer(void *arguments) {

    struct scan_producer_args *args = (struct scan_producer_args*)arguments;
    int pps = args->bfr->pps;

    while (scan_states[args->scan_id] > 0) {
        int total_scans = args->nbr_scans;
        int step = (args->stop - args->start) / total_scans;
        int current = args->start;
        while (total_scans > 0) {
            struct datapoint_nanoVNA_H *data = pull_scan(args->vna_id,current,
                                                        current + step, pps);
            // add to buffer
            if (data)
                add_buff(args->bfr,data);

            // finish loop
            total_scans--;
            current += step;
        }
    }
    args->bfr->complete = true;
    return NULL;
}

void* scan_timer(void *arguments) { 
    struct scan_timer_args *args = (struct scan_timer_args *)arguments;
    sleep(args->time_to_wait);
    scan_states[args->scan_id] = 0;
    printf("---\ntimer done\n---\n");
    return NULL;
}

void* scan_consumer(void *arguments) {

    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;
    int pps = args->bfr->pps;

    FILE *f = args->touchstone_file;
    printf("ID Label VNA TimeSent TimeRecv Freq SParam Format Value\n");

    while (!args->bfr->complete || (args->bfr->count != 0)) {

        struct datapoint_nanoVNA_H *data = take_buff(args->bfr);
        if (!data) {
            // take_buff has returned nothing as there was nothing left to take
            return NULL;
        }

        double send_secs = ((double)(data->send_time.tv_sec - args->program_start_time.tv_sec) + 
                            (double)(data->send_time.tv_usec - args->program_start_time.tv_usec) / 1e6);
        double recv_secs = ((double)(data->receive_time.tv_sec - args->program_start_time.tv_sec) + 
                            (double)(data->receive_time.tv_usec - args->program_start_time.tv_usec) / 1e6);

        for (int i = 0; i < pps; i++) {
            struct nanovna_raw_datapoint *p = &data->point[i];
            
            // Console output
            if (args->verbose) {
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
            }
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

//----------------------------------------
// Touchstone Logic
//----------------------------------------

FILE * create_touchstone_file(struct tm *tm_info) {
    // Create Touchstone file
    char filename[128];
    strftime(filename, sizeof(filename), "vna_scan_at_%Y-%m-%d_%H-%M-%S.s2p", tm_info);

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
    return touchstone_file;
}

//----------------------------------------
// Scan State Logic
//----------------------------------------

/**
 * Initialises scan state arrays if they are not already initialised.
 * 
 * @return EXIT_SUCCESS on success, error code on failure
 */
static int initialise_scan_state() {
    pthread_mutex_lock(&scan_state_lock);
    if (scan_states == NULL) {
        ongoing_scans = 0;
        scan_states = calloc(sizeof(int),MAX_ONGOING_SCANS);
        if (!scan_states) {
            pthread_mutex_unlock(&scan_state_lock);
            return EXIT_FAILURE;
        }
        for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
            scan_states[i] = -1;
        }
    }
    if (scan_threads == NULL) {
        scan_threads = calloc(sizeof(pthread_t),MAX_ONGOING_SCANS);
        if (!scan_threads) {
            free(scan_states);
            scan_states = NULL;
            pthread_mutex_unlock(&scan_state_lock);
            return EXIT_FAILURE;
        }
    }
    pthread_mutex_unlock(&scan_state_lock);
    return EXIT_SUCCESS;
}

/**
 * Allocates and initialises tracking state for a scan
 * 
 * If scan state arrays have not been initialised, calls initialise_scan_state.
 * Looks for unused space in scan state arrays (marked w/ -1), and if
 * it finds any initialises it to 0 and returns its location, and increments ongoing_scans.
 * 
 * @return scan_id, location of scan in scan tracking state structures.
 * If negative, failed to allocate space for scan.
 */
static int initialise_scan() {
    pthread_mutex_lock(&scan_state_lock);
    if (scan_states == NULL) {
        pthread_mutex_unlock(&scan_state_lock);
        if (initialise_scan_state() != EXIT_SUCCESS) {
            fprintf(stderr, "Error initialising scan state tracking\n");
            return -1;
        }
        pthread_mutex_lock(&scan_state_lock);
    }
    if (ongoing_scans >= MAX_ONGOING_SCANS) {
        fprintf(stderr, "Max number of active scans ongoing\n");
        pthread_mutex_unlock(&scan_state_lock);
        return -1;
    }
    int scan_id = -1;
    int i = 0;
    while (i < MAX_ONGOING_SCANS && scan_id < 0) {
        if (scan_states[i] == -1)
            scan_id = i;
        i++;
    }
    if (scan_id < 0 || scan_id >= MAX_ONGOING_SCANS) {
        fprintf(stderr, "how did we get here?\n");
        pthread_mutex_unlock(&scan_state_lock);
        return -1;
    }
    scan_states[scan_id] = 0;
    ongoing_scans++;

    pthread_mutex_unlock(&scan_state_lock);
    return scan_id;
}

/**
 * Resets a finished scan's tracking state and decrements ongoing_scans.
 * 
 * @param scan_id location of finished scan in scan tracking state structures
 */
static void destroy_scan(int scan_id) {
    pthread_mutex_lock(&scan_state_lock);
    scan_states[scan_id] = -1;
    ongoing_scans--;
    pthread_mutex_unlock(&scan_state_lock);
}

//----------------------------------------
// Sweep Logic
//----------------------------------------

struct run_sweep_args {
    int scan_id;
    int nbr_vnas;
    int nbr_scans;
    int start;
    int stop;
    SweepMode sweep_mode;
    int sweeps;
    int pps;
    const char *user_label;
};
void* run_sweep(void* arguments){

    struct run_sweep_args *args = (struct run_sweep_args*)arguments;

    int error;

    struct timeval program_start_time;
    gettimeofday(&program_start_time, NULL);
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    FILE* touchstone_file = create_touchstone_file(tm_info);
    char id_string[64];
    strftime(id_string, sizeof(id_string), "%Y%m%d_%H%M%S", tm_info);

    // Create consumer and producer threads
    struct bounded_buffer *bb = malloc(sizeof(struct bounded_buffer));
    if (!bb) {
        fprintf(stderr, "Failed to allocate memory for bounded buffer construct\n");
        return NULL;
    }
    error = create_bounded_buffer(bb,args->nbr_vnas);
    if (error != 0) {
        fprintf(stderr, "Failed to create bounded buffer\n");
        free(bb);
        bb = NULL;
        return NULL;
    }

    pthread_mutex_lock(&scan_state_lock);
    scan_states[args->scan_id] = args->nbr_vnas;
    pthread_mutex_unlock(&scan_state_lock);
    

    struct scan_producer_args producer_args[args->nbr_vnas];
    pthread_t producers[args->nbr_vnas];
    for (int i = 0; i < args->nbr_vnas; i++) {
        producer_args[i].scan_id = args->scan_id;
        producer_args[i].vna_id = i;
        producer_args[i].nbr_scans = args->nbr_scans;
        producer_args[i].start = args->start;
        producer_args[i].stop = args->stop;
        producer_args[i].nbr_sweeps = args->sweeps;
        producer_args[i].bfr = bb;

        if (args->sweep_mode == NUM_SWEEPS) {
            error = pthread_create(&producers[i], NULL, &scan_producer, &producer_args[i]);
        } else {
            error = pthread_create(&producers[i], NULL, &sweep_producer, &producer_args[i]);
        }

        if(error != 0){
            fprintf(stderr, "Error %i creating producer thread %d: %s\n", errno, i, strerror(errno));
            return NULL;
        }
    }

    pthread_t consumer;
    struct scan_consumer_args consumer_args = {
        bb, 
        touchstone_file,
        id_string,
        (char*)args->user_label,
        true,
        program_start_time
    };
    error = pthread_create(&consumer, NULL, &scan_consumer, &consumer_args);
    if(error != 0){
        fprintf(stderr, "Error %i creating consumer thread: %s\n", errno, strerror(errno));
        destroy_bounded_buffer(bb);
        bb=NULL;
        return NULL;
    }

    if (args->sweep_mode == TIME) {
        sleep(args->sweeps);
        pthread_mutex_lock(&scan_state_lock);
        scan_states[args->scan_id] = args->nbr_vnas;
        pthread_mutex_unlock(&scan_state_lock);
        printf("---\ntimer done\n---\n");
    }

    // wait for threads to finish

    for(int i = 0; i < args->nbr_vnas; i++) {
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
    destroy_bounded_buffer(bb);
    bb = NULL;
    free(arguments);

    return NULL;
}

int start_sweep(int nbr_vnas, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, int pps, const char* user_label) {

    if (nbr_vnas < 1) {
        fprintf(stderr, "No VNAs!\n");
        return -2;
    }

    int scan_id = initialise_scan();

    if (scan_id < 0) {
        return scan_id;
    }

    struct run_sweep_args *args = malloc(sizeof(struct run_sweep_args));
    if (!args) {
        fprintf(stderr, "failed to allocate memory for arguments");
        return -1;
    }
    args->scan_id = scan_id;
    args->nbr_vnas = nbr_vnas;
    args->nbr_scans = nbr_scans;
    args->start = start;
    args->stop = stop;
    args->sweep_mode = sweep_mode;
    args->sweeps = sweeps;
    args->pps = pps;
    args->user_label = user_label;

    pthread_mutex_lock(&scan_state_lock);
    pthread_create(&scan_threads[scan_id],NULL,&run_sweep,args);
    pthread_mutex_unlock(&scan_state_lock);

    return scan_id;
}

int stop_sweep(int scan_id) {
    pthread_mutex_lock(&scan_state_lock);
    if (scan_states == NULL) {
        fprintf(stderr, "Scan array not initialised\n");
        pthread_mutex_unlock(&scan_state_lock);
        return -1;
    }
    if (scan_states[scan_id] == -1) {
        fprintf(stderr, "Not currently scanning\n");
        pthread_mutex_unlock(&scan_state_lock);
        return -1;
    }

    scan_states[scan_id] = 0;
    pthread_mutex_unlock(&scan_state_lock);

    pthread_join(scan_threads[scan_id], NULL);
    destroy_scan(scan_id);
    
    return EXIT_SUCCESS;
}