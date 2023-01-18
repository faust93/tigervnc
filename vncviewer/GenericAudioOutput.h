/* Generic audio implementation using miniaudio library (c) 2022 Faust93
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

#ifndef __DUMBAUDIOOUTPUT_H__
#define __DUMBAUDIOOUTPUT_H__

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <rdr/types.h>

#include "miniaudio.h"

class GenericAudioOutput {

  public:
    static const size_t maxNetworkJitterInMillisec = 1000;

    static const rdr::U8 sampleFormatU8  = 0;
    static const rdr::U8 sampleFormatS8  = 1;
    static const rdr::U8 sampleFormatU16 = 2;
    static const rdr::U8 sampleFormatS16 = 3;
    static const rdr::U8 sampleFormatU32 = 4;
    static const rdr::U8 sampleFormatS32 = 5;

    GenericAudioOutput();
    ~GenericAudioOutput();

    bool isAvailable() const { return haveGO; }
    rdr::U8 getSampleFormat() const { return sampleFormat; }
    rdr::U8 getNumberOfChannels() const { return numberOfChannels; }
    rdr::U32 getSamplingFreq() const { return samplingFreq; }
    size_t getSampleSize() const { return (numberOfChannels << (sampleFormat >> 1)); }

    bool openAndAllocateBuffer();
    bool isOpened() const { return openedGO; }

    void notifyStreamingStartStop(bool isStart);
    void addSilentSamples(size_t numberOfSamples);
    size_t addSamples(const rdr::U8* data, size_t size);

  private:
    static void audioOutCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    bool           haveGO, openedGO, cb_lock;
    ma_device      aDev;
    ma_uint32      frameBytes;
    rdr::U8        sampleFormat, numberOfChannels;
    rdr::U32       samplingFreq;
    rdr::U8*       bufPtr;

    size_t         bufTotalSize;
    size_t         bufFreeSize;
    size_t         bufUnsubmittedSize;
    size_t         bufSubmittedHead;
    size_t         bufUnsubmittedHead;
    rdr::U32       extraDelayInMillisec;
};

#endif // __DUMBAUDIOOUTPUT_H__
