# Test plan: RP2040 dual-core TickTock system


### T1.0 DMA capture health (Core1)
- Track DMA write progress (implementation-specific) and ring high-water mark.
- Track overrun/drop counters.

Pass:
- No DMA overruns at expected edge rates over multi-hour runs.
- Overrun behavior is explicit (counter increments, visible on `/status` and logs).

