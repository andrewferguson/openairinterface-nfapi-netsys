/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */
#include "emuran_bler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "openair2/NR_UE_PHY_INTERFACE/NR_Packet_Drop.h"

typedef enum { DIR_DL = 0, DIR_UL = 1, NUM_DIR = 2 } emuran_dir_t;

typedef struct {
  bool enabled;
  float sinr_db;
  float bler_fixed;   /* <0 disables, use table instead */
  bool retx_never_fail;
  float harq_gain_db;
  long stat_period;
  long trace_n;
} emuran_cfg_t;

typedef struct {
  bool have_first_ts;
  struct timespec first_ts;
  /* window (reset every stat_period counted decisions) */
  long win_tbs, win_fail;
  int mcs_last, mcs_min, mcs_max;
  long win_r[4], win_f[4];
  /* cumulative */
  long tot_tbs, tot_fail, tot_init, tot_retx;
  long trace_count;
} emuran_stats_t;

static emuran_cfg_t g_cfg[NUM_DIR];
static uint64_t g_seed;
static pthread_once_t g_cfg_once = PTHREAD_ONCE_INIT;

static emuran_stats_t g_stats[NUM_DIR];
static pthread_mutex_t g_lock[NUM_DIR] = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };

static const char *dir_name(emuran_dir_t d)
{
  return d == DIR_DL ? "DL" : "UL";
}

static float getenv_float(const char *name, float def)
{
  const char *v = getenv(name);
  if (!v || !*v)
    return def;
  return strtof(v, NULL);
}

static long getenv_long(const char *name, long def)
{
  const char *v = getenv(name);
  if (!v || !*v)
    return def;
  return strtol(v, NULL, 10);
}

static void emuran_load_config(void)
{
  g_seed = (uint64_t)getenv_long("EMURAN_SEED", 0);

  for (int d = 0; d < NUM_DIR; d++) {
    char key[64];
    const char *nm = dir_name((emuran_dir_t)d);
    emuran_cfg_t *c = &g_cfg[d];

    snprintf(key, sizeof(key), "EMURAN_%s_BLER", nm);
    c->enabled = getenv_long(key, 0) != 0;

    snprintf(key, sizeof(key), "EMURAN_%s_SINR_DB", nm);
    c->sinr_db = getenv_float(key, 30.0f);

    snprintf(key, sizeof(key), "EMURAN_%s_BLER_FIXED", nm);
    c->bler_fixed = getenv_float(key, -1.0f);

    snprintf(key, sizeof(key), "EMURAN_%s_RETX_NEVER_FAIL", nm);
    c->retx_never_fail = getenv_long(key, 0) != 0;

    snprintf(key, sizeof(key), "EMURAN_%s_HARQ_GAIN_DB", nm);
    c->harq_gain_db = getenv_float(key, 0.0f);

    snprintf(key, sizeof(key), "EMURAN_%s_STAT_PERIOD", nm);
    c->stat_period = getenv_long(key, 1000);

    snprintf(key, sizeof(key), "EMURAN_%s_TRACE_N", nm);
    c->trace_n = getenv_long(key, 0);
  }
}

/* SplitMix64 -- deterministic, not rand(). */
static inline uint64_t splitmix64_step(uint64_t x)
{
  uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

/* Fold the whole sampling key into one hash: pure function of the inputs,
 * so a decision cannot shift with thread interleaving or arrival order. */
static uint64_t emuran_hash(uint64_t seed, emuran_dir_t dir, uint16_t rnti, uint16_t sfn,
                             uint16_t slot, uint8_t harq_pid, uint8_t rv, uint8_t cw)
{
  uint64_t h = splitmix64_step(seed);
  h = splitmix64_step(h ^ (uint64_t)dir);
  h = splitmix64_step(h ^ (uint64_t)rnti);
  h = splitmix64_step(h ^ (uint64_t)sfn);
  h = splitmix64_step(h ^ (uint64_t)slot);
  h = splitmix64_step(h ^ (uint64_t)harq_pid);
  h = splitmix64_step(h ^ (uint64_t)rv);
  h = splitmix64_step(h ^ (uint64_t)cw);
  return h;
}

/* OAI cycles rv as {0,2,3,1}; map rv -> HARQ round index in that cycle. */
static int rv_to_round(uint8_t rv)
{
  switch (rv) {
    case 0: return 0;
    case 2: return 1;
    case 3: return 2;
    case 1: return 3;
    default: return 0;
  }
}

/* Reuse the pre-existing MCS/RV capture: NR_IF_Module.c's read_channel_param()
 * fills slot_rnti_mcs[] from every dl_tti_request pdsch_pdu, keyed by rnti
 * within the slot. */
static bool lookup_dl_mcs_rv(int slot, uint16_t rnti, uint8_t *mcs_out, uint8_t *rv_out)
{
  if (slot < 0 || slot >= NUM_NFAPI_SLOT)
    return false;
  int num_pdus = slot_rnti_mcs[slot].num_pdus;
  for (int n = 0; n < num_pdus; n++) {
    if (slot_rnti_mcs[slot].rnti[n] == rnti) {
      *mcs_out = slot_rnti_mcs[slot].mcs[n];
      *rv_out = slot_rnti_mcs[slot].rvIndex[n];
      return true;
    }
  }
  return false;
}

static void print_csv_locked(emuran_dir_t dir, const emuran_cfg_t *cfg, emuran_stats_t *st)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  double t = (double)(now.tv_sec - st->first_ts.tv_sec) + (double)(now.tv_nsec - st->first_ts.tv_nsec) / 1e9;
  double win_bler = st->win_tbs > 0 ? (double)st->win_fail / (double)st->win_tbs : 0.0;
  double tot_bler = st->tot_tbs > 0 ? (double)st->tot_fail / (double)st->tot_tbs : 0.0;

  printf("[EMURAN-%s-CSV] t=%.3f win_tbs=%ld win_fail=%ld win_bler=%.5f "
         "mcs_last=%d mcs_min=%d mcs_max=%d "
         "r0=%ld r1=%ld r2=%ld r3=%ld  f0=%ld f1=%ld f2=%ld f3=%ld "
         "tot_tbs=%ld tot_fail=%ld tot_bler=%.5f tot_init=%ld tot_retx=%ld\n",
         dir_name(dir), t, st->win_tbs, st->win_fail, win_bler,
         st->mcs_last, st->mcs_min, st->mcs_max,
         st->win_r[0], st->win_r[1], st->win_r[2], st->win_r[3],
         st->win_f[0], st->win_f[1], st->win_f[2], st->win_f[3],
         st->tot_tbs, st->tot_fail, tot_bler, st->tot_init, st->tot_retx);
  fflush(stdout);

  st->win_tbs = 0;
  st->win_fail = 0;
  memset(st->win_r, 0, sizeof(st->win_r));
  memset(st->win_f, 0, sizeof(st->win_f));
}

/* Core decision: never fails a TB before RA has succeeded (Msg3/Msg4
 * protected); prints nothing and touches no counters when the effective
 * BLER is exactly 0 -- absence of records at high SINR is correct, not a
 * bug. */
static bool emuran_decide_fail(emuran_dir_t dir, uint8_t mcs, uint16_t rnti, uint16_t sfn,
                                uint16_t slot, uint8_t harq_pid, uint8_t rv, uint8_t cw,
                                bool ra_succeeded)
{
  const emuran_cfg_t *cfg = &g_cfg[dir];
  if (!cfg->enabled || !ra_succeeded)
    return false;

  int round = rv_to_round(rv);

  float bler;
  if (cfg->retx_never_fail && round > 0) {
    bler = 0.0f;
  } else if (cfg->bler_fixed >= 0.0f) {
    bler = cfg->bler_fixed;
  } else {
    float eff_sinr_db = cfg->sinr_db + (float)round * cfg->harq_gain_db;
    bler = get_bler_val(mcs, (int)(eff_sinr_db * 10.0f));
  }
  if (bler <= 0.0f)
    return false;
  if (bler > 1.0f)
    bler = 1.0f;

  uint64_t h = emuran_hash(g_seed, dir, rnti, sfn, slot, harq_pid, rv, cw);
  /* top 53 bits -> uniform double in [0,1), standard SplitMix64 recipe */
  double draw = (double)(h >> 11) * (1.0 / 9007199254740992.0);
  bool fail = draw < (double)bler;

  emuran_stats_t *st = &g_stats[dir];
  pthread_mutex_lock(&g_lock[dir]);

  if (!st->have_first_ts) {
    clock_gettime(CLOCK_MONOTONIC, &st->first_ts);
    st->have_first_ts = true;
  }
  if (st->win_tbs == 0) {
    st->mcs_min = mcs;
    st->mcs_max = mcs;
  } else {
    if (mcs < st->mcs_min) st->mcs_min = mcs;
    if (mcs > st->mcs_max) st->mcs_max = mcs;
  }
  st->mcs_last = mcs;
  st->win_tbs++;
  st->tot_tbs++;
  st->win_r[round]++;
  if (round == 0)
    st->tot_init++;
  else
    st->tot_retx++;
  if (fail) {
    st->win_fail++;
    st->tot_fail++;
    st->win_f[round]++;
  }

  if (cfg->trace_n > 0 && st->trace_count < cfg->trace_n) {
    st->trace_count++;
    printf("[EMURAN-%s-TRACE] sfn=%u slot=%u rnti=0x%x harq=%u rv=%u round=%d mcs=%u "
           "bler=%.4f draw=%.4f fail=%d\n",
           dir_name(dir), sfn, slot, rnti, harq_pid, rv, round, mcs, bler, draw, fail ? 1 : 0);
  }

  if (cfg->stat_period > 0 && st->win_tbs >= cfg->stat_period)
    print_csv_locked(dir, cfg, st);

  pthread_mutex_unlock(&g_lock[dir]);

  return fail;
}

bool emuran_dl_should_nack(int sfn, int slot, uint16_t rnti, bool ra_succeeded)
{
  pthread_once(&g_cfg_once, emuran_load_config);
  if (!g_cfg[DIR_DL].enabled)
    return false;

  uint8_t mcs, rv;
  if (!lookup_dl_mcs_rv(slot, rnti, &mcs, &rv))
    return false;

  /* harq_pid isn't resolved yet at this call site (fill_rx_ind() runs before
   * handle_dlsch()'s HARQ-PID resolution) -- sfn/slot/rnti/rv already
   * uniquely identify the TB, so a fixed placeholder keeps the sampling key
   * a pure function of available inputs without needing to plumb the
   * resolved pid back here. */
  return emuran_decide_fail(DIR_DL, mcs, rnti, (uint16_t)sfn, (uint16_t)slot, 0xFF, rv, 0, ra_succeeded);
}

bool emuran_ul_should_fail(int sfn, int slot, uint16_t rnti, uint8_t harq_pid, uint8_t rv,
                            uint8_t mcs, bool ra_succeeded)
{
  pthread_once(&g_cfg_once, emuran_load_config);
  if (!g_cfg[DIR_UL].enabled)
    return false;

  return emuran_decide_fail(DIR_UL, mcs, rnti, (uint16_t)sfn, (uint16_t)slot, harq_pid, rv, 0, ra_succeeded);
}
