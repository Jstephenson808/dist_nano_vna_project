#ifndef VNACOMMANDPARSER_H_
#define VNACOMMANDPARSER_H_

#include "VnaScanMultithreaded.h"
#include "VnaCommunication.h"

#include <string.h>
#include <stdio.h>

// maybe make a struct for the commands I'm passing around?

/**
 * Handles print command
 * 
 * Exprects strtok to be set up by read_command()
 */
void help();

/**
 * Takes strtok args to make a list of vnas
 * 
 * does input validation and checks vnas are connected
 * expects strtok to be set up by read_command()
 * 
 * @param tok the first argument
 * @param vnas the array to put the new list of vnas into
 * @return number of vnas in list
 */
int get_vna_list_from_args(char* tok, int* vnas);

/**
 * Handles scan command
 * 
 * Takes the next argument to determine scan mode, 
 * then the argument(s) after that to determine which vnas to pass in.
 * Uses the parameters set up by set()
 * 
 * Expects strtok to be set up by read_command()
 */
void scan();

/**
 * Handles sweep command
 * 
 * Takes the next argument to determine whether to start, stop, or list scans
 * 
 * start: then takes the argument(s) after that to determine which vnas to pass in.
 * stop: takes the next argument to decide which sweep to stop / all sweeps if NULL.
 * list: lists status of all sweeps
 */
void sweep();

/**
 * Calculates a number of scans and points per scan value given a resolution.
 * 
 * @param res the resolution value to assign other things off of
 * @param nbr_scans the place to put the new nbr_scans value
 * @param points_per_scan the place to put the new points_per_scan value
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on invalid res
 */
int calculate_resolution(int res, int* nbr_scans, int* points_per_scan);

/**
 * Gives a new value to a setting that would be passed into a new scan/sweep
 * 
 * Does input validation itself.
 * Expects strtok to be set up.
 */
void set();

/**
 * Lists the current settings that would be passed into a new scan/sweep
 */
void list();

/**
 * Prints the connected VNAs
 */
void list_vnas();

/**
 * Handles VNA connection-related commands, passing control to
 * relevant VnaCommunication method.
 * 
 * Will take an input for the type of VNA command,
 * then potentially the vna to be targeted.
 * 
 * Expects strtok to be set up by read_command()
 */
void vna_commands();

/**
 * Reads a single command from stdin, sets up strtok and hands
 * exectution over to relevant other function.
 * 
 * @return returns 1 if exit command issued, otherwise returns 0.
 */
int read_command();

/**
 * Initialises all scan() arguments to their default settings,
 * and instructs VnaCommunication to initialise its VNA list.
 * 
 * @return EXIT_SUCCESS on success, else error code
 */
int initialise_settings();

#endif