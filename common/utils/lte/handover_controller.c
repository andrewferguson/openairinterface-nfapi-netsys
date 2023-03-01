#include "handover_controller.h"

const float RSRP_GOOD_MEASUREMENT = 40.0;
long long start_time = 0;
long long last_measurement_time = 0;

long long get_time_s()
{
  struct timeval te;
  gettimeofday(&te, NULL); // get current time
  long long seconds = te.tv_sec; 
  return seconds;
}

void parse_handovers(char *filename)
{
  handover_count = 0;
  char line[256];
  FILE *fptr;

  if ((fptr = fopen(filename, "r")) == NULL) {
    printf("DEBUG: Error! opening %s file\n", filename);
    return;
  }

  int enb_id, time_offset;
  int count = 0;

  while (fgets(line, sizeof(line), fptr)) {
    sscanf(line, "%d,%d", &enb_id, &time_offset);

    handovers[count][0] = enb_id;
    handovers[count][1] = time_offset;
    count++;
  }
  handover_count = count;
  printf("%d Handovers parsed\n", handover_count);
  for (int i = 0; i < handover_count; i++)
  {
    printf("%d,%d\n", handovers[i][0], handovers[i][1]);
  }
  fflush(stdout);
}


void update_target_eNB()
{
  if (start_time == 0) {
    start_time = get_time_s();
  }

  printf("Ioulios: update_target_eNB\n");
  fflush(stdout);

  int current_time;
  int new_target_eNB = 10000;
  int enb_id, time_offset;
  current_time = get_time_s();

  if (current_time == last_measurement_time) {
    printf("skip\n");
    fflush(stdout);
    return;
  }

  for (int i = 0; i < handover_count; i++)
  {
    enb_id = handovers[i][0];
    time_offset = handovers[i][1];

    if (time_offset == 0)
    {
      printf("time offset is 0, set new to %d\n", enb_id);
      fflush(stdout);
      new_target_eNB = enb_id;
    }
    
    if (current_time - start_time < time_offset)
    {
      printf("current_time - start_time\n", start_time, current_time, time_offset);
      printf("start time: %ld, current time:%ld, offset: %d\n", start_time, current_time, time_offset);
      fflush(stdout);
      continue; // change to break to if handovers are ordered by time
    } 
    else
    {
      new_target_eNB = enb_id;
    }
    
  }

  last_measurement_time = current_time;
  if (target_eNB != new_target_eNB) {
    printf("old target: eNB%d, new target: eNB%d\n", target_eNB, new_target_eNB);
    fflush(stdout);
  }
  
  target_eNB = new_target_eNB;

  return;
}

int get_target_eNB()
{
  return target_eNB;
}