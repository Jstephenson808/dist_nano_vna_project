#ifndef VNACOMMANDPARSER_H_
#define VNACOMMANDPARSER_H_

#include "VnaScanMultithreaded.h"

#include <string.h>

// maybe make a struct for the commands I'm passing around?

/*
 * Handles print command
 * 
 * Exprects strtok to be set up by read_command()
 */
void help();

/*
 * Handles scan command
 *
 * Will take a strtok reading (if it exists) to determine scan mode
 * 
 * Expects strtok to be set up by read_command()
 */
void scan();

/*
 * Reads a single command from stdin, sets up strtok and hands
 * exectution over to relevant other function.
 * 
 * @return returns 1 if exit command issued, otherwise returns 0.
 */
int read_command();

/*
 * Initialises all scan() arguments to their default settings,
 * and instructs VnaCommunication to initialise its VNA list.
 */
void initialise_settings();

#endif