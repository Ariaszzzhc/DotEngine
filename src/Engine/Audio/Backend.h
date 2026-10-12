#pragma once

#include <miniaudio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Quiescent graph operations over the pinned backend's exposed sound state.
 * 静止图操作，管理固定版本后端公开声部状态；调用期间不得并发渲染。 */
ma_result dotAudioSeek(ma_sound* sound, ma_uint64 frame);
ma_result dotAudioSetLooping(ma_sound* sound, ma_bool32 looping);
ma_result dotAudioSetPitch(ma_sound* sound, float pitch);

#ifdef __cplusplus
}
#endif
