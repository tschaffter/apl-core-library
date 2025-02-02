/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#ifndef _APL_FAKE_PLAYER_H
#define _APL_FAKE_PLAYER_H

#include <memory>
#include "apl/media/mediaplayer.h"

namespace apl {

/**
 * A model of a media player that plays and repeats a single video track.  The objective
 * is to come a little closer to something like the <video> HTML tag or ExoPlayer with
 * a single repeating track.
 *
 * Create a new FakePlayer for each new video track, passing in the MediaTrack object defined
 * by the APL document author, and parameters for the actual length of the video, how much
 * time spent in buffering before playback starts, and an optional time for when the video should
 * suddenly fail.
 *
 * The player is always created in the IDLE state.  Under normal use, the player should be created
 * and the play() method invoked.  As time is fed to the FakePlayer, the TRACK_READY event will
 * be reported first, followed by some number of TIME_UPDATE events and a TRACK_DONE at the end.
 * If a "failAfter" value has been set and is reached before TRACK_DONE, a TRACK_FAILED event
 * will be generated instead.
 *
 * There are a few special cases to note:
 *
 * 1. The MediaTrack offset and duration set the range of the video that should be played.  It's
 *    possible that these will fall outside of the actual video that is available.  In that case
 *    the video will go from TRACK_READY to TRACK_DONE directly, ignoring all repeats and the
 *    play head position will be set to the length of the video.
 * 1. The current position in the video goes from the start of the playback (generally the track
 *    offset) to the end of the playback (generally the track offset plus the track duration).
 *    However, if the video is looping the play head will always be reset to the start for each
 *    new loop until the last loop, where it will be placed at the end.  For example, if the
 *    video loops twice and has start=0, end=1000 and we advance time by 250 milliseconds each
 *    step, then the following video positions will be reported:  0, 250, 500, 750, 0, 250, 500,
 *    750, 1000.
 * 1. The finish() method puts the video in the DONE state immediately.  However, if the
 *    video length is infinite, the play head position will be set to the starting position. Note
 *    that this position is reported in APL Video events.
 *
 */
class FakePlayer {
public:
    /**
     * Events generated by the FakePlayer as time passes
     */
    enum FakeEvent {
        /**
         * The play head changed position.
         */
        TIME_UPDATE,

        /**
         * The video track has finished buffering and is ready to start playing.
         */
        TRACK_READY,

        /**
         * The video track reached the end.  Note that an infinitely looped video will never
         * issue TRACK_DONE.
         */
        TRACK_DONE,

        /**
         * The video track crashed and has entered the FAILED state
         */
        TRACK_FAIL,

        /**
         * A null reported generated if nothing happens.
         */
        NO_REPORT
    };

    /**
     * Internal state of the FakePlayer
     */
    enum FakeState {
        /**
         * Not attempting to play, but will buffer as time passes.
         */
        IDLE,

        /**
         * Actively buffering or playing the video.  Loops appropriately to match the repeatCount
         * specified in the MediaTrack.  Takes into account the actual start and stop positions
         * of the play head based on the MediaTrack offset/duration and the actual size of the
         * video.
         */
        PLAYING,

        /**
         * The video has finished playing and repeating.  The play head is left at the end of
         * the track with the repeatCounter set to the maximum.  A video in the DONE state can
         * be set back IDLE/PLAYING by rewinding or seeking.
         */
        DONE,

        /**
         * The video crashed.  The play head and repeat counter are left where they were.  A
         * crashed video can never leave the crashed state.
         */
        FAILED
    };

    /**
     * Create a fake player with fake content.
     * @param mediaTrack The instructions from APL for what to play
     * @param actualDuration The actual duration of the content.  May be 0 -> content fails to load
     * @param initialDelay The initial buffering delay (milliseconds)
     * @param failAfter How many milliseconds of playback will succeed before failure.  A negative number
     *                  means the content will never fail.  0 means the content fails after initial buffering.
     * @return The FakePlayer
     */
    static std::unique_ptr<FakePlayer> create(const MediaTrack& mediaTrack,
                                              int actualDuration,
                                              int initialDelay,
                                              int failAfter);

    FakePlayer(int requestedDuration,
               int repeatCount,
               int failAfter,
               int start,
               int duration,
               int initialDelay);

    /**
     * @return The current track state for use in event handlers
     */
    TrackState getTrackState() const {
        if (mState == FAILED)
            return TrackState::kTrackFailed;
        if (mBufferingTime == 0)
            return TrackState::kTrackReady;
        return kTrackNotReady;
    }
    /**
     * @return The current track position
     */
    int getPosition() const { return mTrackPosition; }

    /**
     * @return The requested duration of the current track.  This is not guaranteed to be the
     *         same as the actual playing time of the current track.
     */
    int getDuration() const { return mRequestedDuration; }

    /**
     * @return The internal state of the player
     */
    FakeState getState() const { return mState; }

    /**
     * @return True if this player has not finished or failed yet
     */
    bool active() const { return mState == IDLE || mState == PLAYING; }

    /**
     * @return True if this player is done playing.  It could be DONE or FAILED
     */
    bool isEnded() const { return mState == DONE || mState == FAILED; }

    /**
     * @return True if this player is currently playing content
     */
    bool isPlaying() const { return mState == PLAYING; }

    /**
     * @return True if this player is at the very start and hasn't repeated yet.
     */
    bool atStart() const {
        return mTrackPosition == mStart && mCompletedPlays == 0;
    }

    /**
     * Start playing (or buffering) the contents of the media player
     * @return True if the player actually started playing
     */
    bool play();

    /**
     * Pause the media player
     * @return True if the player actually paused.
     */
    bool pause();

    /**
     * Rewind to the beginning and clear the repeat counter.
     * For now we'll assume that there is no buffering required (may not be true in practice)
     * A video with no duration cannot be rewound after it is marked DONE.
     * @return True if the player actually rewound (i.e., was not at the start)
     */
    bool rewind();

    /**
     * Set the track to done, unless it has previously failed.
     * @return True if the player actually finished.
     */
    bool finish();

    /**
     * Change the play head position.  If the video has no more repeats, seeking to the end of
     * the video will change the internal state to DONE.  If the video was already done, seeking
     * to an earlier spot will set the internal state to IDLE.
     * @param offset The position to seek to (this is relative to the offset and clipped to the range)
     * @return True if the play head moved.
     */
    bool seek(int offset);

    /**
     * Clear the repeat counter (used by the "setTrack" ControlMedia command).
     * @return True if the track was DONE with at least one repeat and now has been reset to IDLE.
     */
    bool clearRepeat();

    /**
     * Advanced time by some number of milliseconds, taking into account video buffering,
     * repeats, etc.  Return the amount of time that actually passed until DONE/FAIL occurred
     * @param milliseconds
     * @return A pair of the event that stopped time and the actual amount of time that passed.
     */
    std::pair<FakeEvent, apl_duration_t> advanceTime(apl_duration_t maxTimeToAdvance);

    /**
     * @return A debugging string
     */
    std::string toDebugString() const;

private:
    bool positionAtEnd(int position) const;
    int clipPosition(int position) const;

private:
    const int mRequestedDuration;  // The requested duration of the track (not the actual; see start/end)
    const int mRepeatCount; // Number of times to repeat.  -1 = repeat forever
    const int mFailAfter;   // After this many milliseconds of playback, fail the video track (-1 = never)
    const int mStart;       // Where we actually start playing from
    const int mDuration;    // Milliseconds to play.  May be -1 to indicate forever.

    int mBufferingTime;  // Amount of time left for buffering content
    int mTrackPosition;  // Position of the play-head in the current track (between start and end)
    int mCompletedPlays; // Number of times we've played through the track

    FakeState mState = IDLE;
    bool mReadyDispatched = false;
};


} // namespace apl

#endif // _APL_FAKE_PLAYER_H
