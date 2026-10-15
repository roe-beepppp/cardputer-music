#pragma once
#include <stdint.h>

namespace ui {

void init();
void tick();
void invalidate();
void toastVolume(uint8_t vol);
void showError(const char* msg);

}
