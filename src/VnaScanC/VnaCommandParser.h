#ifndef VNACOMMANDPARSER_H_
#define VNACOMMANDPARSER_H_

#include "VnaScanMultithreaded.h"
#include <string.h>

// maybe make a struct for the commands I'm passing around?

void help();
void scan();
int readCommand();

#endif