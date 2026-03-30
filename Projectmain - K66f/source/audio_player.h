#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>

void Audio_Player_Init(void);
void Audio_Player_Play(const uint8_t *data, uint32_t length);
int  Audio_Player_IsBusy(void);

#endif /* AUDIO_PLAYER_H */
