/** \file
    \brief TimeSync: Time Synchronization
    \copyright Copyright (c) 2017 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of TimeSync nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include "TimeSync.h"


//------------------------------------------------------------------------------
// WindowedMinTS24

void WindowedMinTS24::Update(
    Counter24 value,
    uint64_t timestamp,
    const uint64_t windowLengthTime)
{
    const Sample sample(value, timestamp);

    // On the first sample, new best sample, or if window length has expired:
    if (!IsValid() ||
        value <= Samples[0].Value ||
        Samples[2].TimeoutExpired(sample.Timestamp, windowLengthTime))
    {
        Reset(sample);
        return;
    }

    // Insert the new value into the sorted array
    if (value <= Samples[1].Value)
        Samples[2] = Samples[1] = sample;
    else if (value <= Samples[2].Value)
        Samples[2] = sample;

    // Expire best if it has been the best for a long time
    if (Samples[0].TimeoutExpired(sample.Timestamp, windowLengthTime))
    {
        // Also expire the next best if needed
        if (Samples[1].TimeoutExpired(sample.Timestamp, windowLengthTime))
        {
            Samples[0] = Samples[2];
            Samples[1] = sample;
        }
        else
        {
            Samples[0] = Samples[1];
            Samples[1] = Samples[2];
        }
        Samples[2] = sample;
        return;
    }

    // Quarter of window has gone by without a better value - Use the second-best
    if (Samples[1].Value == Samples[0].Value &&
        Samples[1].TimeoutExpired(sample.Timestamp, windowLengthTime / 4))
    {
        Samples[2] = Samples[1] = sample;
        return;
    }

    // Half the window has gone by without a better value - Use the third-best one
    if (Samples[2].Value == Samples[1].Value &&
        Samples[2].TimeoutExpired(sample.Timestamp, windowLengthTime / 2))
    {
        Samples[2] = sample;
    }
}


//------------------------------------------------------------------------------
// TimeSynchronizer

void TimeSynchronizer::OnPeerMinDeltaTS24(Counter24 minDeltaTS24)
{
    LastFC_MinDeltaTS24 = minDeltaTS24;
    GotPeerUpdate = true;

    Recalculate();
}

void TimeSynchronizer::OnAuthenticatedDatagram(
    Counter24 remoteSendTS24,
    uint64_t localRecvUsec)
{
    const Counter24 localTS24 = (uint32_t)(localRecvUsec >> kTime23LostBits);

    // OWD_i + ClockDelta(L-R)_i = Local Receive Time - Remote Send Time
    const Counter24 deltaTS24 = localTS24 - remoteSendTS24;

    WindowedMinTS24Deltas.Update(deltaTS24, localRecvUsec, kDriftWindowUsec);

    Recalculate();
}

void TimeSynchronizer::Recalculate()
{
    if (!WindowedMinTS24Deltas.IsValid() || !GotPeerUpdate)
        return;

    // min(OWD_i) + ClockDelta(L-R)_i
    const Counter24 minRecvDeltaTS24 = WindowedMinTS24Deltas.GetBest();

    // min(OWD_j) + ClockDelta(R-L)_j
    const Counter24 minSendDeltaTS24 = LastFC_MinDeltaTS24;

    // Assume min(OWD_i) = min(OWD_j):
    // min(OWD) ~= (min(OWD_j) + min(OWD_i)) / 2
    const Counter23 minOWD_TS23 = (minSendDeltaTS24 + minRecvDeltaTS24).ToUnsigned() >> 1;

    // Assume ClockDelta(R-L)_j = -ClockDelta(L-R)_i:
    // ClockDelta(R-L) ~= (ClockDelta(R-L)_j - ClockDelta(L-R)_i) / 2
    const Counter23 clockDelta_TS23 = (minSendDeltaTS24 - minRecvDeltaTS24).ToUnsigned() >> 1;

    // Convert to Usec
    MinimumOneWayDelayUsec = minOWD_TS23.ToUnsigned() << kTime23LostBits;
    RemoteTimeDeltaUsec = clockDelta_TS23.ToUnsigned() << kTime23LostBits;

    Synchronized = true;
}
