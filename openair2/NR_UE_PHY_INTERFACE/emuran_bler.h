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
#ifndef __EMURAN_BLER_H__
#define __EMURAN_BLER_H__

#include <stdint.h>
#include <stdbool.h>

/* EMURAN channel model: SINR -> BLER (EESM AWGN tables) -> ACK/NACK or CRC
 * status, sampled with a seeded, stateless hash so a decision is a pure
 * function of (seed, direction, rnti, sfn, slot, harq_pid, rv, cw). See
 * docs/how_to_channel for the full spec (env vars, output format, traps).
 *
 * Both entry points are no-ops (return false, i.e. success) unless the
 * corresponding EMURAN_<DIR>_BLER=1 env var is set, and never fail a TB
 * before RA has succeeded.
 */

/* DL: called from NR_IF_Module.c fill_rx_ind() to decide pdsch_pdu.ack_nack
 * (ack_nack=false => NACK). Looks up the MCS/RV captured for (slot, rnti)
 * via the pre-existing slot_rnti_mcs[] mechanism in NR_Packet_Drop.c; if
 * nothing was captured for this TB (e.g. no dl_tti_request pdsch_pdu seen
 * yet for this slot/rnti) this is a no-op. */
bool emuran_dl_should_nack(int sfn, int slot, uint16_t rnti, bool ra_succeeded);

/* UL: called from fapi_nr_ue_l1.c to decide crc_list[].tb_crc_status
 * (return true => tb_crc_status=1, CRC failure). mcs/rv/harq_pid come
 * directly from the PUSCH config PDU at the call site. */
bool emuran_ul_should_fail(int sfn, int slot, uint16_t rnti, uint8_t harq_pid,
                            uint8_t rv, uint8_t mcs, bool ra_succeeded);

#endif
