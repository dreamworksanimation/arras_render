// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <scene_rdl2/common/grid_util/Parser.h>
#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/math/Mat4.h>
#include <scene_rdl2/common/rec_time/RecTime.h>

#include <atomic>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>

namespace scene_rdl2 {
namespace rdl2 {
    class ValueContainerDeq;
    class ValueContainerEnq;
} // namespace rdl2
} // namespace scene_rdl2

namespace arras_render {

class CamPlaybackEvent
{
public:
    using Mat4f = scene_rdl2::math::Mat4f;
    using SendCamCallBack = std::function<void(const Mat4f& camMtx)>;

    CamPlaybackEvent()
        : mEventId(0)
        , mIntervalSec(0.0f)
    {}
    CamPlaybackEvent(const size_t id, const float intervalSec, const Mat4f& camMtx)
        : mEventId(id)
        , mIntervalSec(intervalSec)
        , mCamMtx(camMtx)
    {}

    void setIntervalSec(const float sec) { mIntervalSec = sec; }
    float getIntervalSec() const { return mIntervalSec; }

    void replace(const float intervalSec, const Mat4f& camMtx)
    {
        mIntervalSec += intervalSec;
        mCamMtx = camMtx;
    }

    void playback(const SendCamCallBack& sendCamCallBack,
                  const bool skipInterlva,
                  const float intervalScale) const;

    void encode(scene_rdl2::rdl2::ValueContainerEnq& enq) const;
    void decode(scene_rdl2::rdl2::ValueContainerDeq& deq);

    std::string show() const;

protected:
    size_t mEventId;
    float mIntervalSec;
    scene_rdl2::math::Mat4f mCamMtx;
};

class CamPlayback
//
// This class is used to playback a sequence of camera positions. It is pretty easy to record interactive
// camera movements and save them to the disk. Then, we can load them and playback them. 
// This functionality can rerun the exact same camera path action with the same timing many times.
// This is pretty useful to reshoot runtime screenshots with different mcrt total number configurations.
//
{
public:
    using Mat4f = scene_rdl2::math::Mat4f;
    using SendCamCallBack = CamPlaybackEvent::SendCamCallBack;
    using Parser = scene_rdl2::grid_util::Parser;
    using Arg = scene_rdl2::grid_util::Arg;

    enum class Mode : int {
        MODE_STOP,
        MODE_REC,
        MODE_PLAY,
        MODE_SLIDESHOW
    };

    enum class ThreadState : int { INIT, IDLE, BUSY };

    CamPlayback();
    ~CamPlayback();
    
    void setSendCamCallBack(const SendCamCallBack& sendCamCallBack);

    Mode getMode() const { return mMode; }

    void clear();

    void recInterval(const float sec) { mRecInterval = sec; }
    void recStart() { mTime.start(); mMode = Mode::MODE_REC; }
    void recCam(const Mat4f& camMtx);

    void saveCam(const Mat4f& camMtx); // save camera position for recAdd action
    void recAdd(const float intervalSec);

    void playStart() { mPlayCurrEventId = mStartEventId; mMode = Mode::MODE_PLAY; }
    void playContinue() { mMode = Mode::MODE_PLAY; }

    void slideShow(const float intervalSec);

    void jumpTo(const size_t eventId);
    void editInterval(const float intervalSec);

    void stop() { mMode = Mode::MODE_STOP; }

    bool save(const std::string& filename, std::string& error) const;
    bool load(const std::string& filename, std::string& error);

    std::string show() const;
    std::string showInterval() const;

    static std::string showMode(const Mode& mode);

    Parser& getParser() { return mParser; }

protected:

    bool isMakeNewEvent(const float interval) const;
    void addLast(const float intervalSec, const Mat4f& camMtx);
    void replaceLast(const float intervalSec, const Mat4f& camMtx);
    void setEventRange(const size_t start, const size_t end);
    void resetEventRange();

    void processCurrPlaybackEvent();

    static void threadMain(CamPlayback* camPlayback);

    void parserConfigure();

    //------------------------------

    std::thread mThread;
    std::atomic<ThreadState> mThreadState;
    bool mThreadShutdown;

    mutable std::mutex mMutex;
    std::condition_variable mCvBoot; // using at boot threadMain sequence

    //------------------------------
    
    std::atomic<Mode> mMode;

    scene_rdl2::rec_time::RecTime mTime;
    float mRecInterval; // sec
    std::vector<CamPlaybackEvent> mEvent;
    SendCamCallBack mSendCamCallBack;

    Mat4f mCurrCamMtx;

    bool mLoopPlayback;
    bool mReversePlayback;
    size_t mPlayCurrEventId;
    size_t mStartEventId;
    size_t mEndEventId;
    float mPlayIntervalScale;

    float mInitFrameSec;
    float mLastFrameSec;
    float mSlideShowFrameSec;

    scene_rdl2::grid_util::Parser mParser;
};

} // namespace arras_render
