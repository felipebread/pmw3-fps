#pragma once
#include <stdint.h>

/* The stub preserves all registers, flags, stack, and floating-point state. */
extern uint32_t g_targetBits;
extern volatile long g_hookSeen;
extern void *g_original;
void FrameHook(void);
