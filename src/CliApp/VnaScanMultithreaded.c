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
    uint8_t window[4];
    // Align to the 4-byte header (mask + points)
    if (read_exact(vna_id, window, 4) != 4) {
        fprintf(stderr, "Failed to read initial header bytes\n");
        return EXIT_FAILURE;
    }

    int attempts = 0;
    while (attempts++ < 1000) {
        uint16_t mask = window[0] | (window[1] << 8);
        uint16_t points = window[2] | (window[3] << 8);

        if (mask == expected_mask && points == expected_points) {
            // Header found! The next bytes are the start of the first datapoint.
            if (read_exact(vna_id, (uint8_t*)first_point, sizeof(struct nanovna_raw_datapoint)) 
                != sizeof(struct nanovna_raw_datapoint)) {
                fprintf(stderr, "Failed to read first data point after header\n");
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        }

        // Slide window by 1 byte
        window[0] = window[1];
        window[1] = window[2];
        window[2] = window[3];
        if (read_exact(vna_id, &window[3], 1) != 1) {
            fprintf(stderr, "Timeout/Error sliding header window\n");
            return EXIT_FAILURE;
        }
    }
    
    fprintf(stderr, "Binary header not found after 1000 attempts\n");
    return EXIT_FAILURE;
}

struct datapoint_nanoVNA_H* pull_scan(int vna_id, int start, int stop, int pps) {
    struct timeval send_time, receive_time;
    gettimeofday(&send_time, NULL);

    // Send scan command
    char msg_buff[64];
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, stop, pps, MASK);
    if (write_command(vna_id, msg_buff) < 0) {
        fprintf(stderr, "Failed to send scan command to VNA %d\n", vna_id);
        return NULL;
    }

    // Create struct for data points
    struct datapoint_nanoVNA_H *data = malloc(sizeof(struct datapoint_nanoVNA_H));
    if (!data) return NULL;

    data->point = malloc(sizeof(struct nanovna_raw_datapoint) * pps);
    if (!data->point) {
        free(data);
        return NULL;
    }

    // Find binary header and read first point
    if (find_binary_header(vna_id, &data->point[0], MASK, pps) != EXIT_SUCCESS) {
        free(data->point);
        free(data);
        return NULL;
    }

    // Receive remaining data points
    for (int i = 1; i < pps; i++) {
        ssize_t bytes_read = read_exact(vna_id, 
                                        (uint8_t*)&data->point[i], 
                                        sizeof(struct nanovna_raw_datapoint));
        
        if (bytes_read != sizeof(struct nanovna_raw_datapoint)) {
            fprintf(stderr, "Partial read for data point %d on VNA %d: got %zd of %zu bytes\n", 
                    i, vna_id, bytes_read, sizeof(struct nanovna_raw_datapoint));
            free(data->point);
            free(data);
            return NULL;
        }
    }

    data->vna_id = vna_id;
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
        if (is_running(args->scan_id) == false) break; // Check for stop signal

        int current = args->start;
        int step = (int)round(args->stop - args->start) / ((args->nbr_scans*pps)-1);
        for (int scan = 0; scan < args->nbr_scans; scan++) {
            struct datapoint_nanoVNA_H *data = pull_scan(args->vna_id,current,
                                                        current + step*(pps-1), pps);
            if (data) {
                add_buff(args->bfr,data);
            } else {
                fprintf(stderr, "[Producer] Failed to pull scan data for VNA %d\n", args->vna_id);
                // Depending on requirements, we could break here or continue
            }
            current += step*args->bfr->pps;
        }
    }

    pthread_mutex_lock(&scan_state_lock);
    if (--scan_states[args->scan_id] <= 0)
        args->bfr->complete = true;
    pthread_mutex_unlock(&scan_state_lock);

    free(args); // Free heap-allocated args
    return NULL;
}

void* sweep_producer(void *arguments) {
    struct scan_producer_args *args = (struct scan_producer_args*)arguments;
    int pps = args->bfr->pps;

    while (is_running(args->scan_id)) {
        int total_scans = args->nbr_scans;
        int step = (args->stop - args->start) / total_scans;
        int current = args->start;
        while (total_scans > 0) {
            struct datapoint_nanoVNA_H *data = pull_scan(args->vna_id,current,
                                                        current + step, pps);
            if (data)
                add_buff(args->bfr,data);

            total_scans--;
            current += step;
        }
    }
    args->bfr->complete = true;
    free(args); // Free heap-allocated args
    return NULL;
}

void* scan_timer(void *arguments) { 
    struct scan_timer_args *args = (struct scan_timer_args *)arguments;
    sleep(args->time_to_wait);
    
    pthread_mutex_lock(&scan_state_lock);
    if (scan_states && args->scan_id >= 0 && args->scan_id < MAX_ONGOING_SCANS) {
        if (scan_states[args->scan_id] > 0) scan_states[args->scan_id] = 0;
    }
    pthread_mutex_unlock(&scan_state_lock);
    
    printf("---\ntimer done\n---\n");
    free(args); // Free heap-allocated args
    return NULL;
}

void* scan_consumer(void *arguments) {
    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;
    int pps = args->bfr->pps;

    FILE *f = args->touchstone_file;
    if (args->verbose)
        printf("ID Label VNA TimeSent TimeRecv Freq SParam Format Value\n");

    while (!args->bfr->complete || (args->bfr->count != 0)) {
        struct datapoint_nanoVNA_H *data = take_buff(args->bfr);
        if (!data) break;

        double send_secs = ((double)(data->send_time.tv_sec - args->program_start_time.tv_sec) + 
                            (double)(data->send_time.tv_usec - args->program_start_time.tv_usec) / 1e6);
        double recv_secs = ((double)(data->receive_time.tv_sec - args->program_start_time.tv_sec) + 
                            (double)(data->receive_time.tv_usec - args->program_start_time.tv_usec) / 1e6);

        for (int i = 0; i < pps; i++) {
            struct nanovna_raw_datapoint *p = &data->point[i];
            
            if (args->verbose) {
                printf("%s %s %d %.6f %.6f %u S11 REAL %.10e\n",
                    args->id_string, args->label, data->vna_id, send_secs, recv_secs, p->frequency, p->s11.re);
                printf("%s %s %d %.6f %.6f %u S11 IMG %.10e\n",
                    args->id_string, args->label, data->vna_id, send_secs, recv_secs, p->frequency, p->s11.im);
                printf("%s %s %d %.6f %.6f %u S21 REAL %.10e\n",
                    args->id_string, args->label, data->vna_id, send_secs, recv_secs, p->frequency, p->s21.re);
                printf("%s %s %d %.6f %.6f %u S21 IMG %.10e\n",
                    args->id_string, args->label, data->vna_id, send_secs, recv_secs, p->frequency, p->s21.im);
            }
            if (f) {
                fprintf(f, "%u %.10e %.10e %.10e %.10e 0 0 0 0\n",
                    p->frequency, p->s11.re, p->s11.im, p->s21.re, p->s21.im);
            }
        }

        free(data->point);
        free(data);
    }
    
    free(args); // Free heap-allocated args
    return NULL;
}

// ... (Touchstone Logic remains same)

// ... (Scan State Logic remains same)

//----------------------------------------
// Sweep Logic
//----------------------------------------

struct run_sweep_args {
    int scan_id;
    int nbr_vnas;
    int* vna_list;
    int nbr_scans;
    int start;
    int stop;
    SweepMode sweep_mode;
    int sweeps;
    int pps;
    const char *user_label;
    bool verbose;
};

void* run_sweep(void* arguments){
    struct run_sweep_args *args = (struct run_sweep_args*)arguments;

    struct timeval program_start_time;
    gettimeofday(&program_start_time, NULL);
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    FILE* touchstone_file = create_touchstone_file(tm_info,args->verbose);
    char id_string[64];
    strftime(id_string, sizeof(id_string), "%Y%m%d_%H%M%S", tm_info);

    struct bounded_buffer *bb = malloc(sizeof(struct bounded_buffer));
    if (!bb) {
        fprintf(stderr, "Failed to allocate memory for bounded buffer\n");
        free(args->vna_list); free(args); return NULL;
    }
    if (create_bounded_buffer(bb, args->pps) != 0) {
        fprintf(stderr, "Failed to create bounded buffer\n");
        free(bb); free(args->vna_list); free(args); return NULL;
    }

    pthread_mutex_lock(&scan_state_lock);
    scan_states[args->scan_id] = args->nbr_vnas;
    pthread_mutex_unlock(&scan_state_lock);

    pthread_t producers[args->nbr_vnas];
    for (int i = 0; i < args->nbr_vnas; i++) {
        struct scan_producer_args *p_args = malloc(sizeof(struct scan_producer_args));
        p_args->scan_id = args->scan_id;
        p_args->vna_id = args->vna_list[i];
        p_args->nbr_scans = args->nbr_scans;
        p_args->start = args->start;
        p_args->stop = args->stop;
        p_args->nbr_sweeps = args->sweeps;
        p_args->bfr = bb;

        int error = (args->sweep_mode == NUM_SWEEPS) ? 
            pthread_create(&producers[i], NULL, &scan_producer, p_args) :
            pthread_create(&producers[i], NULL, &sweep_producer, p_args);

        if(error != 0){
            fprintf(stderr, "Error %i creating producer thread %d\n", error, i);
            free(p_args);
        }
    }

    struct scan_consumer_args *c_args = malloc(sizeof(struct scan_consumer_args));
    c_args->bfr = bb;
    c_args->touchstone_file = touchstone_file;
    c_args->id_string = strdup(id_string);
    c_args->label = args->user_label ? strdup(args->user_label) : strdup("");
    c_args->verbose = args->verbose;
    c_args->program_start_time = program_start_time;

    pthread_t consumer;
    if(pthread_create(&consumer, NULL, &scan_consumer, c_args) != 0){
        fprintf(stderr, "Error creating consumer thread\n");
        bb->complete = true;
        free(c_args->id_string); free(c_args->label); free(c_args);
    }

    if (args->sweep_mode == TIME) {
        struct scan_timer_args *t_args = malloc(sizeof(struct scan_timer_args));
        t_args->time_to_wait = args->sweeps;
        t_args->scan_id = args->scan_id;
        pthread_t timer_thread;
        pthread_create(&timer_thread, NULL, &scan_timer, t_args);
        pthread_detach(timer_thread);
    }

    for(int i = 0; i < args->nbr_vnas; i++) {
        pthread_join(producers[i], NULL);
    }
    pthread_join(consumer, NULL);

    if (touchstone_file) fclose(touchstone_file);
    destroy_bounded_buffer(bb);
    free(args->vna_list);
    free(args);
    return NULL;
}

int start_sweep(int nbr_vnas, int* vna_list, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, int pps, const char* user_label, bool verbose) {

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
    args->vna_list = vna_list;
    args->nbr_scans = nbr_scans;
    args->start = start;
    args->stop = stop;
    args->sweep_mode = sweep_mode;
    args->sweeps = sweeps;
    args->pps = pps;
    args->user_label = user_label;
    args->verbose = verbose;

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