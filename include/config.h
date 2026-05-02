#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

/*
 * parsea argc/argv con getopt_long hacia *cfg.
 *
 * Pre:  cfg apunta a un SimConfig válido.
 * Post: los campos de cfg quedan llenos; opciones no reconocidas imprimen a stderr y
 *       retornan -1; --help imprime el uso y retorna 1.
 */
int config_parse_args(int argc, char *argv[], SimConfig *cfg);

/*
 * verifica que todos los valores en *cfg estén dentro de rangos válidos.
 *
 * Pre:  cfg fue llenado por config_parse_args (o manualmente).
 * Post: ante fallo, se escribe un mensaje de error a stderr.
 *
 * Retorna 0 en éxito, -1 ante fallo de validación.
 */
int config_validate(const SimConfig *cfg);

/*
 * imprime un resumen legible de *cfg en stdout.
 *
 * se debe llamar solo antes de lanzar hilos para que no haya concurrencia
 */
void config_print(const SimConfig *cfg);

#endif /* CONFIG_H */
