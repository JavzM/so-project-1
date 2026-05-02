#define _POSIX_C_SOURCE 200809L

#include "logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Estado del singleton.
 * Todas las variables son estaticas; nadie fuera de logger.c las ve
 */
static FILE *g_log_file = NULL;
static bool g_verbose = false;
static bool g_initialized = false;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER; // Más facil, menos seguro pero X

/*
 * Toma CLOCK_MONOTONIC y lo devuelve en milisegundos.
 * Si clock_gettime falla reporta el error y devuelve 0 como fallback.
 *
 * Ojo: No cambiar el reloj porque, CLOCK_MONOTONIC empieza junto con main y ayuda a calcular el tiempo
 * https://www.baeldung.com/linux/timekeeping-clocks esto es dificil, no tocar
 *
 */
static long long monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        return 0;
    }
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

/* Inicializa el Singleton del logger */
int logger_init(const char *path, bool verbose) {
    if (g_initialized) {
        fprintf(stderr, "logger_init: ya estaba inicializado\n");
        return -1;
    }
    if (path == NULL) {
        fprintf(stderr, "logger_init: path es NULL\n");
        return -1;
    }

    g_log_file = fopen(path, "a"); // Abre el file en modo append (No sobreescribe)
    if (g_log_file == NULL) {
        perror("logger_init: fopen");
        return -1;
    }

    g_verbose = verbose;
    g_initialized = true;
    return 0;
}

/* Escribe una linea en el archivo
 *
 * Este metodo no devuelve nada, no puede fallar solo terminar sin hacer nada
 *
 * fmt -> Mensaje de log, los demás argumentos opcionales son valores de cualquier tipo
 *
 * https://dev.to/scovl/creating-a-robust-logging-system-in-c-2fg6
 */
void logger_event(int truck_id, TruckState state, const char *fmt, ...) {
    if (!g_initialized || g_log_file == NULL) { // Validacion
        return;
    }

    long long ts_ms = monotonic_ms();
    const char *state_name = truck_state_name(state);

    char truck_buf[32]; // Tamaño máximo de la linea
    if (truck_id < 0) {
        snprintf(truck_buf, sizeof(truck_buf), "sys");
    } else {
        snprintf(truck_buf, sizeof(truck_buf), "%d", truck_id);
    }

    int lock_rc = pthread_mutex_lock(&g_log_mutex); // Reclama el mutex o espera que se libere
    //Sección crítica
    if (lock_rc != 0) {
        fprintf(stderr, "logger_event: pthread_mutex_lock fallo: %s\n",
                strerror(lock_rc));
        return;
    }

    fprintf(g_log_file, "[%lld] [truck=%s] [%s] ", // Escribe la "firma" de la linea
            ts_ms, truck_buf, state_name);

    // Mapper de argumentos extra. Mapea argumentos extra en la cadena fmt
    // Ojo: Esto es propenso a fallos pero no supe como resolverlo la verdad
    // https://www.cprogramming.com/tutorial/c/lesson17.html
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_file, fmt, ap); // Escribe la descripción del evento
    va_end(ap); // Libera memoria

    fputc('\n', g_log_file);
    fflush(g_log_file);

    // Si --verbose=true imprime a stdout
    // Esto fijo se puede optimizar pero X
    if (g_verbose) {
        fprintf(stdout, "[%lld] [truck=%s] [%s] ",
                ts_ms, truck_buf, state_name);
        va_list ap2;
        va_start(ap2, fmt);
        vfprintf(stdout, fmt, ap2);
        va_end(ap2);
        fputc('\n', stdout);
        fflush(stdout);
    }

    int unlock_rc = pthread_mutex_unlock(&g_log_mutex); // Libera el mutex
    if (unlock_rc != 0) {
        fprintf(stderr, "logger_event: pthread_mutex_unlock fallo: %s\n",
                strerror(unlock_rc));
    }
}

/* Elimina el Singleton */
void logger_close(void) {
    if (!g_initialized) {
        return;
    }
    if (g_log_file != NULL) {
        if (fclose(g_log_file) != 0) {
            perror("logger_close: fclose");
        }
        g_log_file = NULL;
    }

    int rc = pthread_mutex_destroy(&g_log_mutex);
    if (rc != 0 && rc != EBUSY) {
        fprintf(stderr, "logger_close: pthread_mutex_destroy fallo: %s\n",
                strerror(rc));
    }

    g_initialized = false;
    g_verbose = false;
}
