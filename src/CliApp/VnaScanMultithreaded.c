#include "VnaScanMultithreaded.h"
#include <glob.h>

/**
 * Ensures functions which use the static keyword to be private
 * can still be tested
 */
#ifdef TESTSUITE
#define STATIC  
#else
#define STATIC static
#endif

//---------------------------------------------------
// Scan state global variables (access with mutex)
//---------------------------------------------------

int ongoing_scans = 0;
int* scan_states = NULL;
pthread_t* scan_threads = NULL;
pthread_mutex_t scan_state_lock = PTHREAD_MUTEX_INITIALIZER;

//----------------------------------------
// Forward Declarations
//----------------------------------------
STATIC int initialise_scan_state(void);
STATIC int initialise_scan(void);
STATIC void destroy_scan(int scan_id);

//----------------------------------------
// Touchstone Logic
//----------------------------------------

FILE * create_touchstone_file(struct tm *tm_info, bool verbose) {
    char filename[128];
    strftime(filename, sizeof(filename), "vna_scan_at_%Y-%m-%d_%H-%M-%S.s2p", tm_info);

    FILE *touchstone_file = fopen(filename, "w");
    if (!touchstone_file) {
        fprintf(stderr, "Warning: Failed to open %s for writing.\n", filename);
    } else {
        if (verbose) printf("Saving data to: %s\n", filename);
        fprintf(touchstone_file, "! Touchstone file generated from multi-VNA scan\n");
        fprintf(touchstone_file, "! One file containing all VNAS interleaved\n");
        fprintf(touchstone_file, "# Hz S RI R 50\n");
    }
    return touchstone_file;
}

//----------------------------------------
// Scan State Logic
//----------------------------------------

STATIC int initialise_scan_state(void) {
    pthread_mutex_lock(&scan_state_lock);
    if (scan_states == NULL) {
        ongoing_scans = 0;
        scan_states = calloc(sizeof(int), MAX_ONGOING_SCANS);
        if (!scan_states) {
            pthread_mutex_unlock(&scan_state_lock);
            return EXIT_FAILURE;
        }
        for (int i = 0; i < MAX_ONGOING_SCANS; i++) scan_states[i] = -1;
    }
    if (scan_threads == NULL) {
        scan_threads = calloc(sizeof(pthread_t), MAX_ONGOING_SCANS);
        if (!scan_threads) {
            free(scan_states); scan_states = NULL;
            pthread_mutex_unlock(&scan_state_lock);
            return EXIT_FAILURE;
        }
    }
    pthread_mutex_unlock(&scan_state_lock);
    return EXIT_SUCCESS;
}

STATIC int initialise_scan(void) {
    if (initialise_scan_state() != EXIT_SUCCESS) return -1;

    pthread_mutex_lock(&scan_state_lock);
    if (ongoing_scans >= MAX_ONGOING_SCANS) {
        fprintf(stderr, "Max number of active scans ongoing\n");
        pthread_mutex_unlock(&scan_state_lock);
        return -1;
    }
    int scan_id = -1;
    for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
        if (scan_states[i] == -1) {
            scan_id = i;
            break;
        }
    }
    if (scan_id >= 0) {
        scan_states[scan_id] = 0;
        ongoing_scans++;
    }
    pthread_mutex_unlock(&scan_state_lock);
    return scan_id;
}

STATIC void destroy_scan(int scan_id) {
    pthread_mutex_lock(&scan_state_lock);
    if (scan_id >= 0 && scan_id < MAX_ONGOING_SCANS) {
        scan_states[scan_id] = -1;
        ongoing_scans--;
    }
    pthread_mutex_unlock(&scan_state_lock);
}

int get_ongoing_scan_count(void) {
    if (scan_states == NULL) return 0;
    pthread_mutex_lock(&scan_state_lock);
    int count = ongoing_scans;
    pthread_mutex_unlock(&scan_state_lock);
    return count;
}

bool is_running(int scan_id) {
    if (scan_states == NULL || scan_id < 0 || scan_id >= MAX_ONGOING_SCANS) return false;
    pthread_mutex_lock(&scan_state_lock);
    bool running = (scan_states[scan_id] >= 0);
    pthread_mutex_unlock(&scan_state_lock);
    return running;
}

int get_state(int scan_id, char* state_buffer) {
    if (scan_states == NULL || scan_id < 0 || scan_id >= MAX_ONGOING_SCANS) return EXIT_FAILURE;
    pthread_mutex_lock(&scan_state_lock);
    int state = scan_states[scan_id];
    pthread_mutex_unlock(&scan_state_lock);
    if (state == -1) strncpy(state_buffer, "vacant", 7);
    else if (state == 0) strncpy(state_buffer, "idle", 7);
    else if (state > 0) strncpy(state_buffer, "busy", 7);
    else strncpy(state_buffer, "error", 7);
    return EXIT_SUCCESS;
}

//----------------------------------------
// Bounded Buffer Logic
//----------------------------------------

int create_bounded_buffer(struct bounded_buffer *bb, int pps) {
    struct datapoint_nanoVNA_H **buffer = malloc(sizeof(struct datapoint_nanoVNA_H *) * N);
    if (!buffer) return EXIT_FAILURE;
    *bb = (struct bounded_buffer){buffer, 0, 0, 0, pps, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};
    return EXIT_SUCCESS;
}

void destroy_bounded_buffer(struct bounded_buffer *bb) {
    if (!bb) return;
    free(bb->buffer);
    bb->buffer = NULL;
}

void add_buff(struct bounded_buffer *buffer, struct datapoint_nanoVNA_H *data) {
    pthread_mutex_lock(&buffer->lock);
    while (buffer->count == N) {
        pthread_cond_wait(&buffer->take_cond, &buffer->lock);
    }
    buffer->buffer[buffer->in] = data;
    buffer->in = (buffer->in + 1) % N;
    buffer->count++;
    pthread_cond_signal(&buffer->add_cond);
    pthread_mutex_unlock(&buffer->lock);
}

struct datapoint_nanoVNA_H* take_buff(struct bounded_buffer *buffer) {
    pthread_mutex_lock(&buffer->lock);
    while (buffer->count == 0) {
        if (buffer->complete) {
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
    if (read_exact(vna_id, window, 4) != 4) return EXIT_FAILURE;

    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    
    int attempts = 0;
    int max_attempts = 1000;
    int timeout_seconds = 30;  // 30-second total timeout to find header
    
    while (attempts++ < max_attempts) {
        // Check if we've exceeded timeout
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        int elapsed = current_time.tv_sec - start_time.tv_sec;
        if (elapsed > timeout_seconds) {
            fprintf(stderr, "find_binary_header: timeout after %d seconds and %d attempts\n", elapsed, attempts);
            return EXIT_FAILURE;
        }
        
        uint16_t mask = window[0] | (window[1] << 8);
        uint16_t points = window[2] | (window[3] << 8);
        if (mask == expected_mask && points == expected_points) {
            if (read_exact(vna_id, (uint8_t*)first_point, sizeof(struct nanovna_raw_datapoint)) != sizeof(struct nanovna_raw_datapoint))
                return EXIT_FAILURE;
            return EXIT_SUCCESS;
        }
        window[0] = window[1]; window[1] = window[2]; window[2] = window[3];
        if (read_exact(vna_id, &window[3], 1) != 1) return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}

struct datapoint_nanoVNA_H* pull_scan(int vna_id, int start, int stop, int pps) {
    struct timeval send_time, receive_time;
    gettimeofday(&send_time, NULL);
    char msg_buff[64];
    snprintf(msg_buff, sizeof(msg_buff), "scan %d %d %i %i\r", start, stop, pps, MASK);
    if (write_command(vna_id, msg_buff) < 0) return NULL;

    struct datapoint_nanoVNA_H *data = malloc(sizeof(struct datapoint_nanoVNA_H));
    if (!data) return NULL;
    data->point = malloc(sizeof(struct nanovna_raw_datapoint) * pps);
    if (!data->point) { free(data); return NULL; }

    if (find_binary_header(vna_id, &data->point[0], MASK, pps) != EXIT_SUCCESS) {
        free(data->point); free(data); return NULL;
    }

    for (int i = 1; i < pps; i++) {
        if (read_exact(vna_id, (uint8_t*)&data->point[i], sizeof(struct nanovna_raw_datapoint)) != sizeof(struct nanovna_raw_datapoint)) {
            free(data->point); free(data); return NULL;
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
        if (!is_running(args->scan_id)) break;
        int current = args->start;
        int step = (int)round(args->stop - args->start) / ((args->nbr_scans * pps) - 1);
        for (int scan = 0; scan < args->nbr_scans; scan++) {
            struct datapoint_nanoVNA_H *data = pull_scan(args->vna_id, current, current + step * (pps - 1), pps);
            if (data) add_buff(args->bfr, data);
            current += step * pps;
        }
    }
    pthread_mutex_lock(&scan_state_lock);
    if (--scan_states[args->scan_id] <= 0) args->bfr->complete = true;
    pthread_mutex_unlock(&scan_state_lock);
    free(args);
    return NULL;
}

void* sweep_producer(void *arguments) {
    struct scan_producer_args *args = (struct scan_producer_args*)arguments;
    int pps = args->bfr->pps;
    while (is_running(args->scan_id)) {
        int step = (args->stop - args->start) / args->nbr_scans;
        int current = args->start;
        for (int scan = 0; scan < args->nbr_scans; scan++) {
            struct datapoint_nanoVNA_H *data = pull_scan(args->vna_id, current, current + step, pps);
            if (data) add_buff(args->bfr, data);
            current += step;
        }
    }
    args->bfr->complete = true;
    free(args);
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
    free(args);
    return NULL;
}

void* scan_consumer(void *arguments) {
    struct scan_consumer_args *args = (struct scan_consumer_args*)arguments;
    if (args->verbose) printf("ID Label VNA TimeSent TimeRecv Freq SParam Format Value\n");
    while (!args->bfr->complete || (args->bfr->count != 0)) {
        struct datapoint_nanoVNA_H *data = take_buff(args->bfr);
        if (!data) break;
        double s_sec = (double)(data->send_time.tv_sec - args->program_start_time.tv_sec) + (double)(data->send_time.tv_usec - args->program_start_time.tv_usec) / 1e6;
        double r_sec = (double)(data->receive_time.tv_sec - args->program_start_time.tv_sec) + (double)(data->receive_time.tv_usec - args->program_start_time.tv_usec) / 1e6;
        for (int i = 0; i < args->bfr->pps; i++) {
            struct nanovna_raw_datapoint *p = &data->point[i];
            if (args->verbose) {
                printf("%s %s %d %.6f %.6f %u S11 REAL %.10e\n", args->id_string, args->label, data->vna_id, s_sec, r_sec, p->frequency, p->s11.re);
                printf("%s %s %d %.6f %.6f %u S11 IMG %.10e\n", args->id_string, args->label, data->vna_id, s_sec, r_sec, p->frequency, p->s11.im);
                printf("%s %s %d %.6f %.6f %u S21 REAL %.10e\n", args->id_string, args->label, data->vna_id, s_sec, r_sec, p->frequency, p->s21.re);
                printf("%s %s %d %.6f %.6f %u S21 IMG %.10e\n", args->id_string, args->label, data->vna_id, s_sec, r_sec, p->frequency, p->s21.im);
            }
            if (args->touchstone_file) fprintf(args->touchstone_file, "%u %.10e %.10e %.10e %.10e 0 0 0 0\n", p->frequency, p->s11.re, p->s11.im, p->s21.re, p->s21.im);
        }
        free(data->point); free(data);
    }
    free(args->id_string); free(args->label); free(args);
    return NULL;
}

//----------------------------------------
// Sweep Logic
//----------------------------------------

/**
 * Struct to hold arguments for run_sweep thread
 */
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
    struct timeval start_time; gettimeofday(&start_time, NULL);
    time_t now = time(NULL); struct tm *tm_info = localtime(&now);
    FILE* ts_f = create_touchstone_file(tm_info, args->verbose);
    char id_str[64]; strftime(id_str, sizeof(id_str), "%Y%m%d_%H%M%S", tm_info);

    struct bounded_buffer *bb = malloc(sizeof(struct bounded_buffer));
    if (!bb || create_bounded_buffer(bb, args->pps) != 0) {
        free(args->vna_list); free(args); return NULL;
    }

    pthread_mutex_lock(&scan_state_lock);
    scan_states[args->scan_id] = args->nbr_vnas;
    pthread_mutex_unlock(&scan_state_lock);

    pthread_t producers[args->nbr_vnas];
    for (int i = 0; i < args->nbr_vnas; i++) {
        struct scan_producer_args *p_args = malloc(sizeof(struct scan_producer_args));
        *p_args = (struct scan_producer_args){args->scan_id, args->vna_list[i], args->nbr_scans, args->start, args->stop, args->sweeps, bb};
        pthread_create(&producers[i], NULL, (args->sweep_mode == NUM_SWEEPS ? scan_producer : sweep_producer), p_args);
    }

    struct scan_consumer_args *c_args = malloc(sizeof(struct scan_consumer_args));
    *c_args = (struct scan_consumer_args){bb, ts_f, strdup(id_str), args->user_label ? strdup(args->user_label) : strdup(""), args->verbose, start_time};
    pthread_t consumer; pthread_create(&consumer, NULL, scan_consumer, c_args);

    if (args->sweep_mode == TIME) {
        struct scan_timer_args *t_args = malloc(sizeof(struct scan_timer_args));
        t_args->time_to_wait = args->sweeps; t_args->scan_id = args->scan_id;
        pthread_t t_thread; pthread_create(&t_thread, NULL, scan_timer, t_args); pthread_detach(t_thread);
    }

    for(int i = 0; i < args->nbr_vnas; i++) pthread_join(producers[i], NULL);
    pthread_join(consumer, NULL);

    if (ts_f) fclose(ts_f);
    destroy_bounded_buffer(bb); free(args->vna_list); free(args);
    return NULL;
}

int start_sweep(int nbr_vnas, int* vna_list, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, int pps, const char* user_label, bool verbose) {
    if (nbr_vnas < 1) return -2;
    int scan_id = initialise_scan();
    if (scan_id < 0) return scan_id;
    struct run_sweep_args *args = malloc(sizeof(struct run_sweep_args));
    *args = (struct run_sweep_args){scan_id, nbr_vnas, vna_list, nbr_scans, start, stop, sweep_mode, sweeps, pps, user_label, verbose};
    pthread_mutex_lock(&scan_state_lock);
    pthread_create(&scan_threads[scan_id], NULL, run_sweep, args);
    pthread_mutex_unlock(&scan_state_lock);
    return scan_id;
}

int stop_sweep(int scan_id) {
    pthread_mutex_lock(&scan_state_lock);
    if (!scan_states || scan_id < 0 || scan_id >= MAX_ONGOING_SCANS || scan_states[scan_id] == -1) {
        pthread_mutex_unlock(&scan_state_lock); return -1;
    }
    scan_states[scan_id] = 0;
    pthread_mutex_unlock(&scan_state_lock);
    pthread_join(scan_threads[scan_id], NULL);
    destroy_scan(scan_id);
    return EXIT_SUCCESS;
}