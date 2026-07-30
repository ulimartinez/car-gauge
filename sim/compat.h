// Force-included ahead of every translation unit in the sim build (see
// render.sh -include flag). Arduino's real build auto-generates forward
// declarations for every function in the sketch and implicitly pulls in
// stdio via Arduino.h - neither happens under a plain clang++ invocation,
// so this fills in the gap without touching the real screen_*.ino files.
#include <cstdio>

void update_tire_screen();
void update_oil_screen();
void update_gforce_screen();
