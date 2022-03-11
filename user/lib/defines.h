#pragma once

#include <stdbool.h>

#define MAX_TMP_SIZE 50

extern struct configuration conf;

extern char *getInput(unsigned int lung, char *stringa, bool hide);
extern bool yesOrNo(char *domanda, char yes, char no, bool predef, bool insensitive);
extern char multiChoice(char *domanda, char choices[], int num);