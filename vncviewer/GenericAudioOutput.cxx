/* Generic cross-platform audio implementation using miniaudio (c) 2022 faust93
 * Based on Win32AudioOutput by Mikhail Kupchik
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "GenericAudioOutput.h"

//#define MA_DEBUG_OUTPUT
#define MA_NO_WEBAUDIO
#define MA_NO_NULL
#define MA_NO_ENCODING
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

GenericAudioOutput::GenericAudioOutput()
  : haveGO(false), openedGO(false), cb_lock(false), aDev(), sampleFormat(sampleFormatU8), numberOfChannels(0),
    samplingFreq(0), bufPtr(NULL), bufTotalSize(0),
    bufFreeSize(0), bufUnsubmittedSize(0), bufSubmittedHead(0), bufUnsubmittedHead(0),
    extraDelayInMillisec(0)
{
  rdr::U32 s_freq         = 22050;
  rdr::U8 bits_per_sample = 16;
  rdr::U8 n_channels      = 2;

  sampleFormat     = ((bits_per_sample == 8) ? sampleFormatU8 : sampleFormatS16);
  numberOfChannels = n_channels;
  samplingFreq     = s_freq;

  haveGO           = true;
  return;
}

void GenericAudioOutput::audioOutCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
  GenericAudioOutput* gaSelf = reinterpret_cast<GenericAudioOutput*>(pDevice->pUserData);

  if(!gaSelf->bufUnsubmittedSize) // || gaSelf->cb_lock)
    return;

  gaSelf->frameBytes = frameCount * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels);

  size_t io_bytes = gaSelf->bufUnsubmittedSize;
  if(io_bytes > gaSelf->frameBytes)
     io_bytes = gaSelf->frameBytes;

  size_t pOutOffset = 0;
  size_t bytes_left_to_copy = io_bytes;
  while (bytes_left_to_copy != 0) {
      size_t bytes_to_copy = bytes_left_to_copy;

      if (bytes_to_copy + gaSelf->bufSubmittedHead > gaSelf->bufTotalSize)
        bytes_to_copy = gaSelf->bufTotalSize - gaSelf->bufSubmittedHead;
      if (bytes_to_copy == 0)
        return;

      MA_COPY_MEMORY((rdr::U8*)pOutput + pOutOffset, gaSelf->bufPtr + gaSelf->bufSubmittedHead, bytes_to_copy);
      gaSelf->bufSubmittedHead = ((gaSelf->bufSubmittedHead + bytes_to_copy) & (gaSelf->bufTotalSize - 1));
      pOutOffset += bytes_to_copy;
      bytes_left_to_copy -= bytes_to_copy;
      gaSelf->bufUnsubmittedSize -= bytes_to_copy;
      gaSelf->bufFreeSize += bytes_to_copy;
  }

}

bool GenericAudioOutput::openAndAllocateBuffer()
{
  if (!haveGO)
    return false;

  if (openedGO)
     return false;

  // allocate buffer
  size_t buf_estim_size = (4 * maxNetworkJitterInMillisec * samplingFreq) / 1000;

  size_t buf_alloc_size = 1;
  while (buf_alloc_size < buf_estim_size)
    buf_alloc_size <<= 1;

  size_t sample_size = getSampleSize();

  bufPtr = ((rdr::U8*)( calloc(buf_alloc_size, sample_size) ));
  if (bufPtr == NULL)
     return false;

  bufTotalSize = bufFreeSize = buf_alloc_size * sample_size;
  bufUnsubmittedSize = bufSubmittedHead = bufUnsubmittedHead = 0;

  ma_device_config aDevConfig;
  aDevConfig                    = ma_device_config_init(ma_device_type_playback);
  aDevConfig.playback.format    = ma_format_s16;
  aDevConfig.playback.channels  = numberOfChannels;
  aDevConfig.sampleRate         = samplingFreq;
  aDevConfig.dataCallback       = GenericAudioOutput::audioOutCallback;
//  aDevConfig.performanceProfile = ma_performance_profile_conservative;
//  aDevConfig.noFixedSizedCallback = true;
  aDevConfig.periodSizeInMilliseconds = 20;
  aDevConfig.pUserData         = this;

  if (ma_device_init(NULL, &aDevConfig, &aDev) != MA_SUCCESS)
     return false;

  if (ma_device_start(&aDev) != MA_SUCCESS)
     return false;

  openedGO = true;

  return true;
}

void GenericAudioOutput::addSilentSamples(size_t numberOfSamples)
{
  if (openedGO) {
    size_t bytes_left_to_add = numberOfSamples * getSampleSize();
    while (bytes_left_to_add != 0) {
      size_t bytes_to_add = bytes_left_to_add;
      if (bytes_to_add > bufFreeSize)
        bytes_to_add = bufFreeSize;
      if (bytes_to_add + bufUnsubmittedHead > bufTotalSize)
        bytes_to_add = bufTotalSize - bufUnsubmittedHead;
      if (bytes_to_add == 0)
        break;

      memset(bufPtr + bufUnsubmittedHead, ((sampleFormat == sampleFormatU8) ? 0x80 : 0), bytes_to_add);
      bufUnsubmittedHead  = ((bufUnsubmittedHead + bytes_to_add) & (bufTotalSize - 1));
      bufFreeSize        -= bytes_to_add;
      bufUnsubmittedSize += bytes_to_add;
      bytes_left_to_add  -= bytes_to_add;
    }
  }
}

size_t GenericAudioOutput::addSamples(const rdr::U8* data, size_t size)
{
  /* unsubmitted buffer drain */
  if (bufUnsubmittedSize > frameBytes * 5)
     return size;

  cb_lock = true;
  if (openedGO) {
    size_t bytes_left_to_copy = size;
    while (bytes_left_to_copy != 0) {
      size_t bytes_to_copy = bytes_left_to_copy;

      if (bytes_to_copy > bufFreeSize)
        bytes_to_copy = bufFreeSize;
      if (bytes_to_copy + bufUnsubmittedHead > bufTotalSize)
        bytes_to_copy = bufTotalSize - bufUnsubmittedHead;
      if (bytes_to_copy == 0)
        break;

      memcpy(bufPtr + bufUnsubmittedHead, data, bytes_to_copy);
      bufUnsubmittedHead  = ((bufUnsubmittedHead + bytes_to_copy) & (bufTotalSize - 1));
      bufFreeSize        -= bytes_to_copy;
      bufUnsubmittedSize += bytes_to_copy;
      data               += bytes_to_copy;
      bytes_left_to_copy -= bytes_to_copy;
    }
  }
  cb_lock = false;

  return size;
}

void GenericAudioOutput::notifyStreamingStartStop(bool isStart)
{
  if (isStart) {
    // suppress audio stuttering caused by network jitter:
    // add 20+ milliseconds of silence (playback delay) ahead of actual samples
    size_t delay_in_millisec = 20 + extraDelayInMillisec;
    addSilentSamples(delay_in_millisec * samplingFreq / 1000);
  }
}

GenericAudioOutput::~GenericAudioOutput()
{
  if (openedGO) {
    openedGO = false;
    ma_device_uninit(&aDev);
    free(bufPtr);
  }
}
