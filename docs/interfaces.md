# Shared interfaces (Core1 ↔ Core0)


## Note on edge-event acquisition
If raw edge events are captured (for debugging or advanced analysis), they will be acquired on Core1 via **PIO→DMA→RAM ring**.
Core1 may either:
- summarize events into per-swing `sample_core1_t` records (default), or
- expose a second SPSC queue for raw edge events (optional feature).
