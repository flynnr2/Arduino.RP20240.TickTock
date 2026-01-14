# Test plan: RP2040 dual-core TickTock system

Target: Nano RP2040 Connect (arduino-pico), unified PIO→DMA capture for pendulum+PPS.


## DMA capture health tests
### T_DMA_1 Ring health
- Track ring high-water mark, DMA progress, and overrun/drop counters.
Pass: no overruns at expected rates; overruns are explicit and visible.

### T_DMA_2 PPS vs pendulum collision/tie handling
- Create a synthetic case where PPS and a pendulum edge occur within the same tick.
Pass: system treats ties as valid; no spurious jitter alarms; swing reconstruction remains stable.

## Logging schema validation (SwingRecordV1)

### T_LOG_1 PPS stream reconstruction
- Verify `pps_new==1` occurs exactly once per PPS interval (on the first swing after a PPS edge).
- Reconstruct the PPS series from rows where `pps_new==1` and confirm intervals match expected PPS cadence.

Pass:
- `pps_id` increments monotonically with each PPS edge.
- No missing PPS samples when capture is healthy.

### T_LOG_2 gps_state precedence
Force scenarios and verify authoritative state:
- no PPS ever seen → `NO_PPS`
- PPS present but not stable → `ACQUIRING`
- PPS stable → `LOCKED`
- PPS absent but last-good scale exists → `HOLDOVER`
- PPS present but quality fails gates → `BAD_JITTER`

Pass:
- `gps_state` follows precedence rules.
