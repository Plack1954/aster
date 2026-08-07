#ifndef ASTER_CLI_H
#define ASTER_CLI_H

#include <stdio.h>

/* Runs the public Aster command-line driver using caller-owned streams. */
int aster_cli_main(int argc, char **argv, FILE *output, FILE *error);

#endif
