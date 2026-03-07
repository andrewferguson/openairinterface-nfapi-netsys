# Multi-UE Issue Tracker

Last updated: 2026-03-07 (latest gNB log parsed: 6 add / 5 msg4_acked)
Owner: gNB/UE/proxy integration debugging
Status: In Progress

## Scope
- Scenario: 10 connected UEs, SA, nFAPI (`--nfapi VNF`), `--emulate-l1`, proxy in the middle.
- Goal: stable multi-UE operation with consistent aggregate throughput and no control-plane/state-machine collapse.

## Current Summary
- Unknown-RNTI UCI flood is fixed in latest run (`unknown RNTI=0`).
- Current blocker: attach progression stops at 5 `msg4_acked` UEs in latest sampled log.
- New dominant symptom: repeated `RLC max RETX reached on DRB 1` for a single UE (`rnti b51d`).

## Latest Evidence (Current Run)
- Log: `/tmp/gnb_log.txt`
- Size: `2859` lines (`228K`)
- Unknown-RNTI UCI events: `2621`
  - `RNTI 8c0a`: `1932`
  - `RNTI 25e3`: `689`
- Unknown-UCI handlers:
  - `handle_nr_uci_pucch_0_1`: `2325`
  - `handle_nr_uci_pucch_2_3_4`: `296`
- First unknown-RNTI line: `239`
- Last unknown-RNTI line: `2859` (continuous until end of log)

Other warnings in same run:
- `NGAP ... UEAggregateMaximumBitRate` missing: `7`
- `PDCP warning SRB 1 already exist`: `14`
- `RA override: forcing DCI 1_0/0_0 for RA`: `8`

Not observed in same run:
- `Could not find a HARQ process`: `0`
- `Try to add UE ... list is full`: `0`

## Working Hypotheses
1. Stale UCI arrives after UE context release (late feedback path).
2. RNTI mapping mismatch between proxy and gNB for some UE lifecycle transitions.
3. UE/proxy/gNB context creation/removal ordering race causes transient unknown RNTI windows.

## Progress Log
| Date | Change / Check | Result |
|---|---|---|
| 2026-03-07 | Parsed current gNB log for 10-UE run | Confirmed dominant unknown-RNTI UCI flood (`8c0a`, `25e3`) |
| 2026-03-07 | Checked for other fatal signatures | No `HARQ process not found`; no `UE list full` in this log |
| 2026-03-07 | Patched proxy `dequeue_ue_slot_msgs()` timing filter | Added wrap-safe slot delta and strict stale/future filtering (`stale>8` drop, `future<-2` requeue) |
| 2026-03-07 | Patched proxy `oai_slot_aggregate_uci_ind()` | Enforced same-slot aggregation; fixed duplicate check to use each PDU's own `pdu_type`; skip invalid/duplicate PDUs |
| 2026-03-07 | Patched gNB UCI unknown-RNTI logging | Added rate-limited warnings in PUCCH 0/1 and 2/3/4 handlers to prevent log-flood CPU pressure |
| 2026-03-07 | Build verification | `gNB` build successful; local `proxy` build successful |
| 2026-03-07 | Remote proxy sync + build (`uzzu@10.3.1.1`) | Updated `~/proxy-5g/src/nfapi_pnf.c` and confirmed remote `make` success |
| 2026-03-07 | Post-patch gNB log check (`/tmp/gnb_log.txt`, 2005 lines) | `unknown RNTI` dropped from `2621` to `0`; no `HARQ process not found`; new dominant flood is `RLC max RETX reached on DRB 1` (`1767` lines) |
| 2026-03-07 | Added live UE connected-count logs in gNB | New `[UE-COUNT]` logs on add/remove and Msg4 ACK transition (`total`, `msg4_acked`, `cellgroup`) |
| 2026-03-07 | Switched `[UE-COUNT]` output to `printf` | Avoid dependency on INFO log level; counts now print even when INFO logging is disabled |
| 2026-03-07 | Added RNTI to RLC max-RETX error | `max RETX reached on SRB/DRB` now includes offending UE RNTI for faster root-cause triage |
| 2026-03-07 | Parsed latest `/tmp/gnb_log.txt` (`257` lines, `24K`) | `ue_add=6`, `ue_msg4_acked=5`, `unknown RNTI=0`, `DRB max RETX=17` all on `rnti b51d` |
| 2026-03-07 | Increased RA process pool | `NR_NB_RA_PROC_MAX` raised `4 -> 16` to avoid attach bottleneck during 10-UE bursts |
| 2026-03-07 | Added Msg4 ACK timeout cleanup | On `WAIT_Msg4_ACK` timeout (100 ms), recycle HARQ pid, clear RA context, and remove stale UE context |
| 2026-03-07 | Build verification after RA fixes | `gNB` build successful (`BUILD SHOULD BE SUCCESSFUL`) |
| 2026-03-07 | Parsed latest short gNB capture (`237` lines, `22K`) | `ue_add=6`, `ue_msg4_acked=5` (`972a` pending), `unknown RNTI=0`, `HARQ-not-found=0`, `max RETX SRB/DRB=0`, `Msg4 ACK timeout=0` |
| 2026-03-07 | Parsed console tee capture (longer attach window) | `ue_add=10`, `ue_msg4_acked=6`, then repeated `add->remove` for UE7-UE10 (steady connected count remains `6`) |
| 2026-03-07 | Msg4 emulation-tolerance patch | In `--emulate-l1`, Msg4 ACK timeout and missing `Msg4_ACKed` flag now assume ACK instead of removing UE; remove-on-missing-flag path dropped |
| 2026-03-07 | Build verification after Msg4 patch | `gNB` build successful (`BUILD SHOULD BE SUCCESSFUL`) |
| 2026-03-07 | Reduced BWP config log noise + immediate UE-COUNT flush | `[BWP-CFG]` prints disabled by default; added `fflush(stdout)` after `[UE-COUNT]` prints in gNB MAC |
| 2026-03-07 | Parsed `/tmp/gnb_log.txt` (`433` lines, `34K`) after Msg4 patch | `ue_add=5`, `ue_msg4_acked=5`, `unknown RNTI=0`, `HARQ-not-found=0`, but `DRB max RETX=190` split across `rnti 924b` and `rnti f76d` |
| 2026-03-07 | Parsed `/tmp/gnb_log.txt` (`227` lines, `21K`) latest run | `ue_add=10`, `ue_msg4_acked=9` (missing explicit `2ffd`), `ue_remove=0`, `unknown RNTI=0`, `HARQ-not-found=0`, `SRB max RETX=3` only on `rnti 2ffd` |
| 2026-03-07 | Added RA recovery retry/cleanup patch (UE+gNB) | gNB now does bounded Msg4 retry then removes stale UE context; UE now clears stale RA temp state before fresh RACH retry; gNB+UE rebuilds successful |

## Latest Post-Patch Run (gNB Log)
- Log: `/tmp/gnb_log.txt`
- Size/lines: `101K`, `2005` lines
- Unknown-RNTI UCI:
  - `unknown RNTI`: `0` (baseline was `2621`)
  - `Could not find a HARQ process`: `0`
  - `PUCCH slot mismatch`: `0`
- New dominant error:
  - `RLC max RETX reached on DRB 1`: `1767` lines
  - starts around line `239` and continues densely to end of log
- Additional warnings still present:
  - `NGAP ... UEAggregateMaximumBitRate`: `7`
  - `PDCP SRB 1 already exist`: `14`

## Latest Snapshot (2026-03-07 10:29 UTC)
- Log: `/tmp/gnb_log.txt`
- Size/lines: `24K`, `257` lines
- UE progression from `[UE-COUNT]`:
  - `event=add`: `6` UEs (`b51d`, `7eff`, `1891`, `8409`, `62c5`, `499e`)
  - `event=msg4_acked`: `5` UEs (`b51d`, `7eff`, `1891`, `8409`, `62c5`)
  - Added but not Msg4-acked in this log: `499e`
- Error counters:
  - `unknown RNTI`: `0`
  - `Could not find a HARQ process`: `0`
  - `max RETX reached on SRB 1`: `0`
  - `max RETX reached on DRB 1`: `17` (all `rnti b51d`)
- Other warnings:
  - `NGAP ... UEAggregateMaximumBitRate` still appears
  - `PDCP warning SRB 1 already exist` appears for UE IDs `1,2,3,4,5,6,8` (two each)

## Latest Snapshot (2026-03-07 10:41 UTC)
- Log: `/tmp/gnb_log.txt`
- Size/lines: `22K`, `237` lines
- UE progression from `[UE-COUNT]`:
  - `event=add`: `6` UEs (`ec9f`, `2636`, `18f3`, `2312`, `3c98`, `972a`)
  - `event=msg4_acked`: `5` UEs (`ec9f`, `2636`, `18f3`, `2312`, `3c98`)
  - Added but not Msg4-acked in this capture: `972a`
- Error counters:
  - `unknown RNTI`: `0`
  - `Could not find a HARQ process`: `0`
  - `max RETX reached on SRB 1`: `0`
  - `max RETX reached on DRB 1`: `0`
  - `Msg4 ACK timeout`: `0`
- Notes:
  - Capture ends immediately after the `6th` add line, so this log does not yet include a post-add outcome for `972a`.
  - `PDCP warning SRB 1 already exist` still appears (12 lines total).

## Latest Snapshot (Console Tee, 2026-03-07)
- Source: user-provided console output captured with `tee` (longer than `/tmp/gnb_log.txt`)
- UE progression from `[UE-COUNT]`:
  - UE1..UE6: `event=add` followed by `event=msg4_acked`
  - UE7..UE10: each shows `event=add` followed by `event=remove` without `msg4_acked`
  - Final steady state in this capture: `total=6`, `msg4_acked=6`
- Interpretation:
  - RA/Msg3 works for all attempts (new RNTIs allocated and contexts added).
  - Failure point is Msg4 completion for later UEs under higher concurrent load.

## Next Actions
1. Re-run 10-UE attach and confirm `[UE-COUNT] event=msg4_acked` reaches `10` (or close) within first attach window.
2. Check that no RA processes stay stuck in `WAIT_Msg4_ACK` beyond timeout (look for new timeout warning and immediate UE recycle).
3. Re-check dominant failure after RA scaling (`max RETX on DRB 1` by RNTI) and confirm whether it shifts to fewer UEs.
4. Keep unknown-RNTI monitoring in place to ensure previous proxy/gNB UCI fix remains stable.

### UE Count Monitoring (Added)
- gNB now prints lines like:
  - `[UE-COUNT] event=add ... total=X msg4_acked=Y cellgroup=Z`
  - `[UE-COUNT] event=msg4_acked ... total=X msg4_acked=Y cellgroup=Z`
  - `[UE-COUNT] event=remove ... total=X msg4_acked=Y cellgroup=Z`
- `msg4_acked` is the best "actually connected" count at MAC side for this flow.

## Exit Criteria
- Unknown-RNTI UCI events near zero in steady state.
- 10 UEs remain connected without repeated control/state churn.
- Stable throughput across repeated 30-60s runs without collapse.

## Latest Snapshot (2026-03-07 11:03 UTC, `/tmp/gnb_log.txt`)
- File: `/tmp/gnb_log.txt` (`517` lines, `37K`)
- Counters:
  - `ue_add=10`
  - `ue_msg4_acked=9` (one UE add without explicit msg4_acked line)
  - `ue_remove=0`
  - `unknown RNTI=0`
  - `Could not find a HARQ process=0`
  - `Msg4 ACK timeout=0`
  - `RA Procedure failed at Msg4=0`
  - `max RETX reached on SRB 1=0`
  - `max RETX reached on DRB 1=303`
- DRB max-RETX concentration:
  - `rnti cbe3: 299`
  - `rnti 9f0a: 4`

Interpretation:
- UCI/RA identity mismatch issue remains mitigated.
- Current dominant failure is user-plane reliability for specific UE(s), not unknown-RNTI/HARQ lookup failures.

## Patch Applied (2026-03-07)
- gNB: increased connected-mode emulation feedback floor:
  - `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c`
  - `minfbtime` floor `11 -> 13` for `emulate_l1 && is_common==0`.
- UE runtime repo (`/home/ubuntu/openairinterface5g`): emulation-safe HARQ pre-ACK at DCI scheduling:
  - `openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c` in `set_harq_status()`
  - set `ack_received=true; ack=1;` when `emulate_l1`.

Validation Plan:
1. Rebuild gNB and UE, rerun 10-UE attach.
2. Confirm `event=msg4_acked` reaches 10.
3. Re-check if DRB max-RETX concentration on a single RNTI disappears or shrinks substantially.

## 2026-03-07 Rollback (UE-side pre-ACK)
- Symptom after UE patch: only part of UEs reached UPF even when gNB-side attach counters looked high.
- Cause: UE DCI-time pre-ACK (`ack_received=true` in `set_harq_status()`) can ACK Msg4/data before decode, producing false-positive attach progression at gNB.
- Fix: reverted UE pre-ACK in `/home/ubuntu/openairinterface5g/openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c`.
- Status: UE rebuilt successfully; gNB-side `minfbtime>=13` retained.
