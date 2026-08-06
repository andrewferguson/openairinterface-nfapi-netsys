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

#include "ran_func_rc.h"
#include "../../flexric/test/rnd/fill_rnd_data_rc.h"
#include "../../flexric/src/sm/rc_sm/ie/ir/lst_ran_param.h"
#include "../../flexric/src/sm/rc_sm/ie/ir/ran_param_list.h"
#include "../../flexric/src/agent/e2_agent_api.h"

#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

/* EMURAN: closed-loop xApp control -- actually retargets MAC link
 * adaptation instead of just printf'ing, under style=1 act_id=10:
 *   ran_param 11 = DL max MCS
 *   ran_param 12 = UL max MCS
 *   ran_param 13 = DL BLER target (x1000), e.g. 50 for 0.05
 * The BLER target is represented internally as an [lower, upper]
 * hysteresis band (see get_mcs_from_bler() in
 * gNB_scheduler_primitives.c); this preserves the config default's
 * band half-width (upper=0.15, lower=0.05 -> half-width 0.05) around
 * whatever new target the xApp requests. */
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac_extern.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

/* EMURAN: half-width 0.05 (matching the config default's 0.05-0.15 band)
 * makes lower == 0.0 exactly for the paper's 0.05 target, and
 * get_mcs_from_bler()'s "raise MCS" condition is bler < lower -- with
 * lower == 0.0 that can never be true (bler is never negative), so MCS
 * can only ratchet down and never recover. Use a narrower half-width so
 * lower stays strictly positive for both paper targets (0.05 -> 0.03,
 * 0.20 -> 0.18). */
#define EMURAN_BLER_TARGET_HALF_WIDTH 0.02

void read_rc_sm(void* data)
{
  assert(data != NULL);
//  assert(data->type == RAN_CTRL_STATS_V1_03);
  assert(0!=0 && "Not implemented");
}

void read_rc_setup_sm(void* data)
{
  assert(data != NULL);
//  assert(data->type == RAN_CTRL_V1_3_AGENT_IF_E2_SETUP_ANS_V0);
  rc_e2_setup_t* rc = (rc_e2_setup_t*)data;
  rc->ran_func_def = fill_rc_ran_func_def();
}

sm_ag_if_ans_t write_ctrl_rc_sm(void const* data)
{
  assert(data != NULL);
//  assert(data->type == RAN_CONTROL_CTRL_V1_03 );

  rc_ctrl_req_data_t const* ctrl = (rc_ctrl_req_data_t const*)data;
  if(ctrl->hdr.format == FORMAT_1_E2SM_RC_CTRL_HDR){
    if(ctrl->hdr.frmt_1.ric_style_type == 1 && ctrl->hdr.frmt_1.ctrl_act_id == 2){
      printf("QoS flow mapping configuration \n");
      e2sm_rc_ctrl_msg_frmt_1_t const* frmt_1 = &ctrl->msg.frmt_1;
      for(size_t i = 0; i < frmt_1->sz_ran_param; ++i){
        seq_ran_param_t const* rp = frmt_1->ran_param;
        if(rp[i].ran_param_id == 1){
          assert(rp[i].ran_param_val.type == ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE );
          printf("DRB ID %ld \n", rp[i].ran_param_val.flag_true->int_ran);
        } else if(rp[i].ran_param_id == 2){
          assert(rp[i].ran_param_val.type == LIST_RAN_PARAMETER_VAL_TYPE);
          printf("List of QoS Flows to be modified \n");
          for(size_t j = 0; j < ctrl->msg.frmt_1.ran_param[i].ran_param_val.lst->sz_lst_ran_param; ++j){ 
            lst_ran_param_t const* lrp = rp[i].ran_param_val.lst->lst_ran_param;
            // The following assertion should be true, but there is a bug in the std
            // check src/sm/rc_sm/enc/rc_enc_asn.c:1085 and src/sm/rc_sm/enc/rc_enc_asn.c:984 
            // assert(lrp[j].ran_param_id == 3); 
            assert(lrp[j].ran_param_struct.ran_param_struct[0].ran_param_id == 4) ;
            assert(lrp[j].ran_param_struct.ran_param_struct[0].ran_param_val.type == ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE);

            int64_t qfi = lrp[j].ran_param_struct.ran_param_struct[0].ran_param_val.flag_true->int_ran;
            assert(qfi > -1 && qfi < 65);

            assert(lrp[j].ran_param_struct.ran_param_struct[1].ran_param_id == 5);
            assert(lrp[j].ran_param_struct.ran_param_struct[1].ran_param_val.type == ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE);
            int64_t dir = lrp[j].ran_param_struct.ran_param_struct[1].ran_param_val.flag_false->int_ran;
            assert(dir == 0 || dir == 1);
            printf("qfi = %ld dir %ld \n", qfi, dir);
          }
        }
      }
    } else if(ctrl->hdr.frmt_1.ric_style_type == 1 && ctrl->hdr.frmt_1.ctrl_act_id == 10){
      printf("[EMURAN RC] scheduler-visible control (style=1 act=10)\n");
      e2sm_rc_ctrl_msg_frmt_1_t const* frmt_1 = &ctrl->msg.frmt_1;
      for(size_t i = 0; i < frmt_1->sz_ran_param; ++i){
        seq_ran_param_t const* rp = frmt_1->ran_param;
        if(rp[i].ran_param_val.type != ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE
           && rp[i].ran_param_val.type != ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE)
          continue;
        int64_t val = rp[i].ran_param_val.flag_true->int_ran;

        gNB_MAC_INST *nr_mac = RC.nrmac[0];
        if (nr_mac == NULL) {
          printf("[EMURAN RC] RC.nrmac[0] not yet initialized, dropping control\n");
          continue;
        }

        if(rp[i].ran_param_id == 11){ // DL max MCS
          NR_SCHED_LOCK(&nr_mac->sched_lock);
          printf("[EMURAN RC] dl_bler.max_mcs %d -> %ld\n", nr_mac->dl_bler.max_mcs, val);
          nr_mac->dl_bler.max_mcs = (uint8_t)val;
          NR_SCHED_UNLOCK(&nr_mac->sched_lock);
        } else if(rp[i].ran_param_id == 12){ // UL max MCS
          NR_SCHED_LOCK(&nr_mac->sched_lock);
          printf("[EMURAN RC] ul_bler.max_mcs %d -> %ld\n", nr_mac->ul_bler.max_mcs, val);
          nr_mac->ul_bler.max_mcs = (uint8_t)val;
          NR_SCHED_UNLOCK(&nr_mac->sched_lock);
        } else if(rp[i].ran_param_id == 13){ // DL BLER target (x1000)
          double target = val / 1000.0;
          double lower = target - EMURAN_BLER_TARGET_HALF_WIDTH;
          double upper = target + EMURAN_BLER_TARGET_HALF_WIDTH;
          if (lower < 0.0) lower = 0.0;
          if (upper > 1.0) upper = 1.0;
          NR_SCHED_LOCK(&nr_mac->sched_lock);
          printf("[EMURAN RC] dl_bler target -> %.3f (lower %.3f -> %.3f, upper %.3f -> %.3f)\n",
                 target, nr_mac->dl_bler.lower, lower, nr_mac->dl_bler.upper, upper);
          nr_mac->dl_bler.lower = lower;
          nr_mac->dl_bler.upper = upper;
          NR_SCHED_UNLOCK(&nr_mac->sched_lock);
        }
      }
    }
  }

  sm_ag_if_ans_t ans = {.type = CTRL_OUTCOME_SM_AG_IF_ANS_V0};
  ans.ctrl_out.type = RAN_CTRL_V1_3_AGENT_IF_CTRL_ANS_V0;
  return ans;
}

static
void* emulate_rrc_msg(void* ptr)
{
  uint32_t* ric_id = (uint32_t*)ptr; 
  for(size_t i = 0; i < 5; ++i){
    usleep(rand()%4000);
    rc_ind_data_t* d = calloc(1, sizeof(rc_ind_data_t)); 
    assert(d != NULL && "Memory exhausted");
    *d = fill_rnd_rc_ind_data();
    async_event_agent_api(*ric_id, d);
    printf("Event for RIC Req ID %u generated\n", *ric_id);
  }

  free(ptr);
  return NULL;
}

static
pthread_t t_ran_ctrl;

sm_ag_if_ans_t write_subs_rc_sm(void const* src)
{
  assert(src != NULL); // && src->type == RAN_CTRL_SUBS_V1_03);

  wr_rc_sub_data_t* wr_rc = (wr_rc_sub_data_t*)src;
  printf("ric req id %d \n", wr_rc->ric_req_id);

  uint32_t* ptr = malloc(sizeof(uint32_t));
  assert(ptr != NULL);
  *ptr = wr_rc->ric_req_id;

  int rc = pthread_create(&t_ran_ctrl, NULL, emulate_rrc_msg, ptr);
  assert(rc == 0);

  sm_ag_if_ans_t ans = {0}; 

  return ans;
}

