# Test plan: RP2040 dual-core TickTock system


## DMA capture health tests
### T_DMA_1 Ring health
- Track ring high-water mark, DMA progress, and overrun/drop counters.
Pass: no overruns at expected rates; overruns are explicit and visible.

### T_DMA_2 PPS vs pendulum collision/tie handling
- Create a synthetic case where PPS and a pendulum edge occur within the same tick.
Pass: system treats ties as valid; no spurious jitter alarms; swing reconstruction remains stable.
