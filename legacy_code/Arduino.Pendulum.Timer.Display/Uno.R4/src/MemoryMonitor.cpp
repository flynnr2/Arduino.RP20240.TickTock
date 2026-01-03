#include "MemoryMonitor.h"
#include "Display.h"
#include <cstdio>

namespace MemoryMonitor {

constexpr unsigned long RAM_CHECK_INTERVAL_MS = 2000;

enum RamState { RAM_OK, RAM_LOW, RAM_CRIT };
static RamState ramState = RAM_OK;
static unsigned long lastRamCheck = 0;

extern "C" char* sbrk(int);
int freeRam() {
  char stack_dummy;
  return &stack_dummy - sbrk(0);
}

void poll() {
  if (millis() - lastRamCheck < RAM_CHECK_INTERVAL_MS) return;
  lastRamCheck = millis();
  int fr = freeRam();
  RamState newState = (fr < RAM_CRIT_THRESHOLD) ? RAM_CRIT : (fr < RAM_WARN_THRESHOLD ? RAM_LOW : RAM_OK);
  if (newState != ramState) {
    ramState = newState;
    char msg[40];
    switch (ramState) {
      case RAM_OK:
        snprintf(msg, sizeof(msg), "RAM OK: %d bytes", fr);
        break;
      case RAM_LOW:
        snprintf(msg, sizeof(msg), "RAM LOW: %d bytes", fr);
        break;
      case RAM_CRIT:
        snprintf(msg, sizeof(msg), "RAM CRIT: %d bytes", fr);
        break;
    }
    Display::scrollLog(msg);
  }
}

void serviceBlink() {
  // LED matrix blinking removed; RAM warnings are now logged to the display.
}

} // namespace MemoryMonitor
