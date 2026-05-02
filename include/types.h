#ifndef TYPES_H
#define TYPES_H

#include <pthread.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Enums
 * ------------------------------------------------------------------------- */

typedef enum {
    TRUCK_NEW,
    TRUCK_READY,
    TRUCK_RUNNING,
    TRUCK_BLOCKED,
    TRUCK_TERMINATED
} TruckState;

typedef enum {
    CARGO_GENERAL,
    CARGO_PERISHABLE  /* carga perecedera, prioridad alta */
} CargoType;

typedef enum {
    ALGO_FIFO,
    ALGO_RR
} SchedAlgo;

/* Retorna el nombre legible de un TruckState. Static para leerse desde afuera */
static inline const char *truck_state_name(TruckState s) {
    switch (s) {
        case TRUCK_NEW: return "NEW";
        case TRUCK_READY: return "READY";
        case TRUCK_RUNNING: return "RUNNING";
        case TRUCK_BLOCKED: return "BLOCKED";
        case TRUCK_TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

/* Retorna el nombre legible de un SchedAlgo. Static para leerse desde afuera */
static inline const char *schedule_algorithm_name(SchedAlgo s) {
    switch (s) {
    case ALGO_FIFO: return "FIFO";
    case ALGO_RR: return "ROUND ROBIN";
    default: return "UNKNOWN";
    }
}

/* SimConfig — todos los parámetros de ejecución, llenados por config_parse_args() */
typedef struct {
    SchedAlgo   algorithm;              /* FIFO o RR */
    int                  num_trucks;          /* cantidad de hilos de camión a crear */
    int                  num_docks;          /* capacidad del semáforo (muelle) */
    int                  quantum_ms;       /* quantum de RR en milisegundos */
    bool               priority_enabled; /* ordenamiento perecederos-primero */
    int                  burst_min_ms;     /* burst mínimo de CPU por camión */
    int                  burst_max_ms;    /* burst máximo de CPU por camión */
    int                  arrival_max_ms;  /* retardo máximo entre llegadas (para que no lleguen todos los camiones al mismo tiempo) */
    double          perishable_ratio; /* fracción de camiones con carga perecedera (probabilidad de que el camion tenga prioridad) */
    char               log_file[256];        /* ruta al archivo de log */
    unsigned      seed;                       /* semilla aleatoria */
    bool               verbose;                 /* también imprimir eventos de log a stderr */
} SimConfig;

/* Truck — estado por hilo; creado por simulation, pasado a truck_run() */
typedef struct {
    int                    id;                           /* identificador único del camión [0..N-1] */
    CargoType     cargo;                   /* tipo de carga (afecta la prioridad) */
    int                    burst_ms;            /* trabajo total de CPU requerido (ms) */
    int                    remaining_ms;   /* burst restante tras interrupciones */
    int                    priority;                /* 1=perecedero, 0=general (talvez se pueda eliminar esta variable) */
    TruckState    state;                     /* estado actual del ciclo de vida */
    pthread_t      thread;                  /* handle del hilo POSIX */
    long long       ts_arrival;             /* cuando el camión entró a la cola READY */
    long long       ts_first_run;        /* cuando el camión entró por primera vez a RUNNING */
    long long       ts_terminated;   /* cuando el camión alcanzó TERMINATED */
    void                *sim_ctx;             /* puntero a SimContext en simulation.c */
} Truck;

#endif /* TYPES_H */
