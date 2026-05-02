#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>

/* Valores por defecto */
#define DEFAULT_ALGORITHM       ALGO_FIFO
#define DEFAULT_NUM_TRUCKS      10
#define DEFAULT_NUM_DOCKS       3
#define DEFAULT_QUANTUM_MS      200
#define DEFAULT_BURST_MIN_MS    500
#define DEFAULT_BURST_MAX_MS    2000
#define DEFAULT_ARRIVAL_MAX_MS  300
#define DEFAULT_PERISHABLE_RATIO 0.3
#define DEFAULT_LOG_FILE        "terminal.log"

/* Salida de --help | -h */
static void print_usage(const char *prog) {
    fprintf(stdout,
        "Uso: %s [opciones]\n"
        "\n"
        "Simula una terminal de carga automatizada con planificación de camiones.\n"
        "\n"
        "Opciones:\n"
        "  -a, --algorithm <fifo|rr>   Algoritmo de planificación (defecto: fifo)\n"
        "  -t, --trucks <N>            Cantidad de hilos de camión (defecto: 10)\n"
        "  -d, --docks <M>             Cantidad de muelles disponibles (defecto: 3)\n"
        "  -q, --quantum <ms>          Quantum de RR en milisegundos (defecto: 200, ignorado en FIFO)\n"
        "  -p, --priority              Activa prioridad perecederos-primero\n"
        "      --burst-min <ms>        Burst mínimo por camión (defecto: 500)\n"
        "      --burst-max <ms>        Burst máximo por camión (defecto: 2000)\n"
        "      --arrival-max <ms>      Retardo máximo entre llegadas en ms (defecto: 300)\n"
        "      --perishable-ratio <r>  Fracción de camiones perecederos 0..1 (defecto: 0.3)\n"
        "  -l, --log-file <ruta>       Archivo de log (defecto: terminal.log)\n"
        "  -s, --seed <N>              Semilla aleatoria (defecto: basada en tiempo)\n"
        "  -v, --verbose               También imprime eventos de log a stdout\n"
        "  -h, --help                  Muestra esta ayuda y termina\n",
        prog);
}

/* Tabla de opciones largas para getopt_long */
static const struct option long_opts[] = {
    { "algorithm",       required_argument, NULL, 'a' },
    { "trucks",          required_argument, NULL, 't' },
    { "docks",           required_argument, NULL, 'd' },
    { "quantum",         required_argument, NULL, 'q' },
    { "priority",        no_argument,       NULL, 'p' },
    { "burst-min",       required_argument, NULL,  1  },
    { "burst-max",       required_argument, NULL,  2  },
    { "arrival-max",     required_argument, NULL,  3  },
    { "perishable-ratio",required_argument, NULL,  4  },
    { "log-file",        required_argument, NULL, 'l' },
    { "seed",            required_argument, NULL, 's' },
    { "verbose",         no_argument,       NULL, 'v' },
    { "help",            no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 }
};

/* Auxiliar: parsea un entero positivo de str; retorna -1 si hay error */
static int parse_positive_int(const char *str, const char *name) {
    char *end;
    errno = 0;
    long val = strtol(str, &end, 10);
    if (errno != 0 || end == str || *end != '\0') {
        fprintf(stderr, "Error: '%s' no es un entero valido para %s\n", str, name);
        return -1;
    }
    if (val <= 0) {
        fprintf(stderr, "Error: %s debe ser > 0 (se recibio %ld)\n", name, val);
        return -1;
    }
    return (int)val;
}

/* Auxiliar: parsea un entero no-negativo de str; retorna -1 si hay error */
static int parse_nonneg_int(const char *str, const char *name) {
    char *end;
    errno = 0;
    long val = strtol(str, &end, 10);
    if (errno != 0 || end == str || *end != '\0') {
        fprintf(stderr, "Error: '%s' no es un entero valido para %s\n", str, name);
        return -1;
    }
    if (val < 0) {
        fprintf(stderr, "Error: %s debe ser >= 0 (se recibio %ld)\n", name, val);
        return -1;
    }
    return (int)val;
}

/* Auxiliar: parsea un double entre 0 y 1; asigna *out y retorna 0, o retorna -1 */
static int parse_ratio(const char *str, const char *name, double *out) {
    char *end;
    errno = 0;
    double val = strtod(str, &end);
    if (errno != 0 || end == str || *end != '\0') {
        fprintf(stderr, "Error: '%s' no es un numero valido para %s\n", str, name);
        return -1;
    }
    if (val < 0.0 || val > 1.0) {
        fprintf(stderr, "Error: %s debe estar entre 0 y 1 (se recibio %g)\n", name, val);
        return -1;
    }
    *out = val;
    return 0;
}

/* Parsea los argumentos del programa. Devuelve -1 si hay error
 *
 * Si se recibe un valor incorrecto de semilla, se aplica la semilla por defecto
 */
int config_parse_args(int argc, char *argv[], SimConfig *cfg) {
    /* Aplicar valores por defecto */
    cfg->algorithm        = DEFAULT_ALGORITHM;
    cfg->num_trucks       = DEFAULT_NUM_TRUCKS;
    cfg->num_docks        = DEFAULT_NUM_DOCKS;
    cfg->quantum_ms       = DEFAULT_QUANTUM_MS;
    cfg->priority_enabled = false;
    cfg->burst_min_ms     = DEFAULT_BURST_MIN_MS;
    cfg->burst_max_ms     = DEFAULT_BURST_MAX_MS;
    cfg->arrival_max_ms   = DEFAULT_ARRIVAL_MAX_MS;
    cfg->perishable_ratio = DEFAULT_PERISHABLE_RATIO;
    strncpy(cfg->log_file, DEFAULT_LOG_FILE, sizeof(cfg->log_file) - 1); // copia el nombre del archivo de log por defecto, limitando al tamaño del buffer menos 1 para reservar espacio al '\0'
    cfg->log_file[sizeof(cfg->log_file) - 1] = '\0';
    cfg->seed             = (unsigned)time(NULL);
    cfg->verbose          = false;

    int opt;
    int tmp;
    while ((opt = getopt_long(argc, argv, "a:t:d:q:pl:s:vh", long_opts, NULL)) != -1) { // Se usa getopt_long de getopt.h para que sea mas sencillo usar opciones y argumentos: https://man7.org/linux/man-pages/man3/getopt.3.html
        switch (opt) {
            case 'a':
                if (strcmp(optarg, "fifo") == 0) { // compara el argumento recibido con "fifo" para seleccionar el algoritmo FIFO
                    cfg->algorithm = ALGO_FIFO;
                } else if (strcmp(optarg, "rr") == 0) { // compara el argumento recibido con "rr" para seleccionar el algoritmo Round Robin
                    cfg->algorithm = ALGO_RR;
                } else {
                    fprintf(stderr,
                        "Error: algoritmo desconocido '%s'. Valores validos: fifo, rr\n", optarg);
                    return -1;
                }
                break;

            case 't':
                tmp = parse_positive_int(optarg, "--trucks");
                if (tmp < 0) return -1;
                cfg->num_trucks = tmp;
                break;

            case 'd':
                tmp = parse_positive_int(optarg, "--docks");
                if (tmp < 0) return -1;
                cfg->num_docks = tmp;
                break;

            case 'q':
                tmp = parse_positive_int(optarg, "--quantum");
                if (tmp < 0) return -1;
                cfg->quantum_ms = tmp;
                break;

            case 'p':
                cfg->priority_enabled = true;
                break;

            case 1: /* --burst-min */
                tmp = parse_positive_int(optarg, "--burst-min");
                if (tmp < 0) return -1;
                cfg->burst_min_ms = tmp;
                break;

            case 2: /* --burst-max */
                tmp = parse_positive_int(optarg, "--burst-max");
                if (tmp < 0) return -1;
                cfg->burst_max_ms = tmp;
                break;

            case 3: /* --arrival-max */
                tmp = parse_nonneg_int(optarg, "--arrival-max");
                if (tmp < 0) return -1;
                cfg->arrival_max_ms = tmp;
                break;

            case 4: /* --perishable-ratio */
                if (parse_ratio(optarg, "--perishable-ratio",
                                &cfg->perishable_ratio) != 0) {
                    return -1;
                }
                break;

            case 'l':
                strncpy(cfg->log_file, optarg, sizeof(cfg->log_file) - 1);
                cfg->log_file[sizeof(cfg->log_file) - 1] = '\0';
                break;

            case 's': {
                char *end;
                errno = 0;
                long sv = strtol(optarg, &end, 10);
                if (errno != 0 || end == optarg || *end != '\0' || sv < 0) {
                    fprintf(stderr,
                        "Advertencia: '%s' no es una semilla valida; se usara la semilla por defecto (%u)\n",
                        optarg, cfg->seed);
                } else {
                    cfg->seed = (unsigned)sv;
                }
                break;
            }

            case 'v':
                cfg->verbose = true;
                break;

            case 'h':
                print_usage(argv[0]);
                return 1;

            default:
                fprintf(stderr, "Ejecute '%s --help' para ver el uso.\n", argv[0]);
                return -1;
        }
    }

    /* Advertir (sin fallar) si --quantum fue especificado con FIFO */
    if (cfg->algorithm == ALGO_FIFO && cfg->quantum_ms != DEFAULT_QUANTUM_MS) {
        fprintf(stderr,
            "Advertencia: --quantum se ignora cuando --algorithm es fifo.\n");
    }

    return 0;
}

/* Valido la configuración -> Tests
 *
 * Al final lo dejo porque no afecta tenerlo
 *
 * Ojo: Si se cambia algo en los datos esperados esto va a fallar
 *
 */
int config_validate(const SimConfig *cfg) {
    int ok = 0;

    if (cfg->num_trucks <= 0) {
        fprintf(stderr, "Error: --trucks debe ser > 0\n");
        ok = -1;
    }
    if (cfg->num_docks <= 0) {
        fprintf(stderr, "Error: --docks debe ser > 0\n");
        ok = -1;
    }
    if (cfg->quantum_ms <= 0) {
        fprintf(stderr, "Error: --quantum debe ser > 0\n");
        ok = -1;
    }
    if (cfg->burst_min_ms <= 0) {
        fprintf(stderr, "Error: --burst-min debe ser > 0\n");
        ok = -1;
    }
    if (cfg->burst_max_ms <= 0) {
        fprintf(stderr, "Error: --burst-max debe ser > 0\n");
        ok = -1;
    }
    if (cfg->burst_min_ms > cfg->burst_max_ms) {
        fprintf(stderr, "Error: --burst-min (%d) debe ser <= --burst-max (%d)\n",
                cfg->burst_min_ms, cfg->burst_max_ms);
        ok = -1;
    }
    if (cfg->arrival_max_ms < 0) {
        fprintf(stderr, "Error: --arrival-max debe ser >= 0\n");
        ok = -1;
    }
    if (cfg->perishable_ratio < 0.0 || cfg->perishable_ratio > 1.0) {
        fprintf(stderr, "Error: --perishable-ratio debe estar en [0, 1]\n");
        ok = -1;
    }
    if (cfg->log_file[0] == '\0') {
        fprintf(stderr, "Error: la ruta de --log-file no puede estar vacia\n");
        ok = -1;
    }

    return ok;
}

/* Imprime la configuracion */
void config_print(const SimConfig *cfg) {
    const char *algo_name = (cfg->algorithm == ALGO_FIFO) ? "fifo" : "rr";

    printf("===== Configuracion =====\n");
    printf("  Algoritmo:         %s\n", algo_name);
    printf("  Camiones:          %d\n", cfg->num_trucks);
    printf("  Muelles:           %d\n", cfg->num_docks);
    if (cfg->algorithm == ALGO_RR) {
        printf("  Quantum (ms):      %d\n", cfg->quantum_ms);
    } else {
        printf("  Quantum (ms):      N/A\n");
    }
    printf("  Prioridad:         %s\n", cfg->priority_enabled ? "on" : "off");
    printf("  Burst min (ms):    %d\n", cfg->burst_min_ms);
    printf("  Burst max (ms):    %d\n", cfg->burst_max_ms);
    printf("  Llegada max (ms):  %d\n", cfg->arrival_max_ms);
    printf("  Ratio perecedero:  %.2f\n", cfg->perishable_ratio);
    printf("  Log:               %s\n", cfg->log_file);
    printf("  Semilla:           %u\n", cfg->seed);
    printf("  Verbose:           %s\n", cfg->verbose ? "on" : "off");
    printf("=========================\n");
}
