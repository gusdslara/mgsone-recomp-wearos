#ifndef MGS_WEAR_INPUT_H
#define MGS_WEAR_INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// PS1 Controller Pad State
typedef struct {
    uint16_t buttons;      // Active-low PS1 button bitmask
    uint8_t  left_stick_x; // 0..255, 128 = centered
    uint8_t  left_stick_y; // 0..255, 128 = centered
    uint8_t  right_stick_x;
    uint8_t  right_stick_y;
} MGSWearPadState;

void MGSWear_InitInput(void);
void MGSWear_PollInput(MGSWearPadState *pad);
bool MGSWear_IsFirstPersonMode(void);
bool MGSWear_IsInCodec(void);
int  MGSWear_GetScreenMode(void);
void MGSWear_CycleScreenMode(void);

#ifdef __cplusplus
}
#endif

#endif // MGS_WEAR_INPUT_H
