#define MINIAUDIO_IMPLEMENTATION
#include "Backend.h"

ma_result dotAudioSeek(ma_sound* sound, ma_uint64 frame) {
  ma_result status = ma_resampler_reset(&sound->engineNode.resampler);

  if (status != MA_SUCCESS) {
    return status;
  }

  status = ma_data_source_seek_to_pcm_frame(sound->pDataSource, frame);

  if (status != MA_SUCCESS) {
    return status;
  }

  sound->processingCacheFramesRemaining = 0;
  ma_atomic_exchange_64(&sound->seekTarget, MA_SEEK_TARGET_NONE);
  ma_atomic_exchange_32(&sound->atEnd, MA_FALSE);
  return ma_node_set_time(sound, frame);
}

ma_result dotAudioSetLooping(ma_sound* sound, ma_bool32 looping) {
  if (ma_sound_is_looping(sound) == looping) {
    return MA_SUCCESS;
  }

  /** Rewind unread input rather than replaying already-prefetched loop data.
   * 回退未消费输入，避免继续播放已经预取的循环数据。 */
  if (sound->processingCacheFramesRemaining != 0) {
    ma_uint64 length = 0;
    ma_uint64 cursor = 0;
    ma_uint64 unread = sound->processingCacheFramesRemaining;
    ma_result status = ma_data_source_get_length_in_pcm_frames(sound->pDataSource, &length);

    if (status != MA_SUCCESS || length == 0) {
      return status == MA_SUCCESS ? MA_INVALID_OPERATION : status;
    }

    status = ma_data_source_get_cursor_in_pcm_frames(sound->pDataSource, &cursor);

    if (status != MA_SUCCESS) {
      return status;
    }

    if (ma_sound_is_looping(sound)) {
      cursor = (cursor + length - unread % length) % length;
    } else {
      if (unread > cursor) {
        return MA_INVALID_OPERATION;
      }

      cursor -= unread;
    }

    status = ma_data_source_seek_to_pcm_frame(sound->pDataSource, cursor);

    if (status != MA_SUCCESS) {
      return status;
    }

    sound->processingCacheFramesRemaining = 0;
  }

  ma_sound_set_looping(sound, looping);
  return MA_SUCCESS;
}

ma_result dotAudioSetPitch(ma_sound* sound, float pitch) {
  const ma_bool32 doppler
      = ma_sound_is_spatialization_enabled(sound) && ma_sound_get_doppler_factor(sound) > 0;

  if (doppler) {
    ma_engine* engine = ma_sound_get_engine(sound);
    ma_spatializer_listener* listener = &engine->listeners[ma_sound_get_listener_index(sound)];
    sound->engineNode.spatializer.dopplerPitch = ma_doppler_pitch(
        ma_vec3f_sub(ma_spatializer_listener_get_position(listener), ma_sound_get_position(sound)),
        ma_sound_get_velocity(sound), ma_spatializer_listener_get_velocity(listener),
        ma_spatializer_listener_get_speed_of_sound(listener), ma_sound_get_doppler_factor(sound));
  } else {
    sound->engineNode.spatializer.dopplerPitch = 1;
  }

  const ma_bool32 bypass
      = !doppler && pitch == 1
        && sound->engineNode.sampleRate == ma_engine_get_sample_rate(sound->engineNode.pEngine);
  const ma_bool32 previous = ma_atomic_load_32(&sound->engineNode.isPitchDisabled);

  if (bypass != previous) {
    const ma_result status = ma_resampler_reset(&sound->engineNode.resampler);

    if (status != MA_SUCCESS) {
      return status;
    }
  }

  ma_sound_set_pitch(sound, pitch);
  ma_atomic_exchange_32(&sound->engineNode.isPitchDisabled, bypass);
  return MA_SUCCESS;
}
