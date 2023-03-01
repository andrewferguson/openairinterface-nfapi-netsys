#ifndef HANDOVER_CONTROLLER_H
#define HANDOVER_CONTROLLER_H

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include "rrc_defs.h"
#include "rrc_extern.h"

const float RSRP_GOOD_MEASUREMENT;

int handovers[1024][2];
int handover_count;
int target_eNB;
extern long long start_time;

long long get_time_s();

void parse_handovers(char * filename);

void update_target_eNB();

int get_target_eNB();

#endif /* HANDOVER_CONTROLLER_H*/