# Distributed L2 Emulation - Debug Findings

## Setup
- **Mode**: Distributed L2 emulation (NFAPI STANDALONE_PNF)
- **UE Command**: `sudo taskset -c 2-50 chrt -f 99 ./nr-uesoftmodem -O ./ue.conf --nfapi STANDALONE_PNF --node-number 2 --emulate-l1 --sa 1 0 0`
- **UE Config**: `/home/ubuntu/openairinterface5g/cmake_targets/ran_build/build/ue.conf`
- **gNB remote address**: 10.3.1.1:50601/50611
- **UE local address**: 10.2.2.2:50600/50610

## Errors Found

### Error 1: DLSCH ACK/NACK HARQ timing race
- **Message**: `[NR_MAC] DLSCH ACK/NACK reporting initiated for harq pid X before DLSCH decoding completed for now marking as compelete`
- **Source**: `openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c:2287`
- **Root Cause**: In `NRUE_phy_stub_standalone_pnf_task()` (executables/nr-ue.c), the UL scheduler (`nr_ue_ul_scheduler`) was called BEFORE `process_queued_nr_nfapi_msgs()`. This meant PUCCH HARQ feedback was generated before DLSCH data from gNB was processed, so `ack_received` was always `false`.
- **Impact**: UE always sends NACK (ack=0) instead of ACK, causing continuous retransmissions

## Fixes Applied

### Fix 1: Reorder DL/UL processing in NFAPI PNF task
- **File**: `executables/nr-ue.c` (NRUE_phy_stub_standalone_pnf_task)
- **Change**: Moved `process_queued_nr_nfapi_msgs()` to run BEFORE `nr_ue_ul_scheduler()` 
- **Result**: HARQ ACK/NACK errors eliminated (0 occurrences after fix vs continuous spam before)
- **Verified**: After gNB restart + UE reconnect, UE ran stable for 30+ seconds with 0 errors, 0 HARQ warnings, 74 total log lines (no spam)
