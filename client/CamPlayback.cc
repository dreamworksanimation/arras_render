// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "CamPlayback.h"

#include <scene_rdl2/render/util/StrUtil.h>
#include <scene_rdl2/scene/rdl2/ValueContainerDeq.h>
#include <scene_rdl2/scene/rdl2/ValueContainerEnq.h>

#include <fstream>
#include <iomanip>
#include <sstream>

#include <sys/stat.h> // struct stat

namespace arras_render {

void
CamPlaybackEvent::playback(const SendCamCallBack& sendCamCallBack,
                           const bool skipInterval,
                           const float intervalScale) const
{
    std::ostringstream ostr;
    ostr << ">> CamPlayback.cc CamPlaybackEvent::playback() : eventId:" << mEventId;
    if (!skipInterval) {
        ostr << " interval:" << scene_rdl2::str_util::secStr(mIntervalSec);
        usleep(static_cast<unsigned int>(mIntervalSec * intervalScale * 1000000.0f));
    }
    std::cerr << ostr.str() << '\n';

    if (sendCamCallBack) {
        sendCamCallBack(mCamMtx);
    }
}

void
CamPlaybackEvent::encode(scene_rdl2::rdl2::ValueContainerEnq& enq) const
{
    enq.enqVLSizeT(mEventId);
    enq.enq<float>(mIntervalSec);
    enq.enqMat4f(mCamMtx);
}

void
CamPlaybackEvent::decode(scene_rdl2::rdl2::ValueContainerDeq& deq)
{
    mEventId = deq.deqVLSizeT();
    mIntervalSec = deq.deq<float>();
    mCamMtx = deq.deqMat4f();
}

std::string
CamPlaybackEvent::show() const
{
    auto showVL = [](const float v0, const float v1, const float v2, const float v3) -> std::string {
        auto showV = [](const float v) -> std::string {
            std::ostringstream ostr;
            ostr << std::setw(10) << std::fixed << std::setprecision(5) << v;
            return ostr.str();
        };
        std::ostringstream ostr;
        ostr << showV(v0) << ' ' << showV(v1) << ' ' << showV(v2) << ' ' << showV(v3);
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "CamPlaybackEvent {\n"
         << "  mEventId:" << mEventId << '\n'
         << "  mIntervalSec:" << mIntervalSec << '\n'
         << "  mCamMtx {\n"
         << "    " << showVL(mCamMtx[0][0], mCamMtx[0][1], mCamMtx[0][2], mCamMtx[0][3]) << '\n'
         << "    " << showVL(mCamMtx[1][0], mCamMtx[1][1], mCamMtx[1][2], mCamMtx[1][3]) << '\n'
         << "    " << showVL(mCamMtx[2][0], mCamMtx[2][1], mCamMtx[2][2], mCamMtx[2][3]) << '\n'
         << "    " << showVL(mCamMtx[3][0], mCamMtx[3][1], mCamMtx[3][2], mCamMtx[3][3]) << '\n'
         << "  }\n"
         << "}";
    return ostr.str();
}

//------------------------------------------------------------------------------------------

CamPlayback::CamPlayback()
    : mThreadState(ThreadState::INIT)
    , mThreadShutdown(false)
    , mMode(Mode::MODE_STOP)
    , mRecInterval(1.0f/24.0f)
    , mSendCamCallBack(nullptr)
    , mLoopPlayback(false)
    , mReversePlayback(false)
    , mPlayCurrEventId(0)
    , mStartEventId(0)
    , mEndEventId(0)
    , mPlayIntervalScale(1.0f)
    , mInitFrameSec(5.0f)
    , mLastFrameSec(5.0f)
    , mSlideShowFrameSec(1.0f)
{
    parserConfigure();

    // We have to build thread after finish mCvBoot initialization completed
    mThread = std::move(std::thread(threadMain, this));

    // Wait until thread is booted
    std::unique_lock<std::mutex> uqLock(mMutex);
    mCvBoot.wait(uqLock, [&]{
            return (mThreadState != ThreadState::INIT); // Not wait if already non INIT condition
        });
}

CamPlayback::~CamPlayback()
{
    mThreadShutdown = true; // This is a only place set true to mThreadShutdown
    if (mThread.joinable()) {
        mThread.join();
    }
}

void
CamPlayback::setSendCamCallBack(const SendCamCallBack& sendCamCallBack)
{
    mSendCamCallBack = sendCamCallBack;
}

void
CamPlayback::clear()
{
    stop();
    mEvent.clear();

    mPlayCurrEventId = 0;
    mStartEventId = 0;
    mEndEventId = 0;
}

void
CamPlayback::recCam(const scene_rdl2::math::Mat4f& camMtx)
{
    float currInterval = mTime.end();
    if (isMakeNewEvent(currInterval)) {
        addLast(currInterval, camMtx);
    } else {
        replaceLast(currInterval, camMtx);
    }
    mTime.start();
}

void
CamPlayback::saveCam(const Mat4f& camMtx)
{
    mCurrCamMtx = camMtx;
}

void
CamPlayback::recAdd(const float intervalSec)
{
    addLast(intervalSec, mCurrCamMtx);
}
    
void
CamPlayback::slideShow(const float intervalSec)
{
    mPlayCurrEventId = mStartEventId;
    mMode = Mode::MODE_SLIDESHOW;
    mSlideShowFrameSec = intervalSec;
}

void
CamPlayback::jumpTo(const size_t eventId)
{
    if (!mEvent.size()) return; // skip setup if event data is empty
    mPlayCurrEventId = std::min(eventId, mEvent.size() - 1);

    const CamPlaybackEvent& currEvent = mEvent[mPlayCurrEventId];
    if (mMode == Mode::MODE_PLAY) {
        currEvent.playback(mSendCamCallBack, false, mPlayIntervalScale);
    } else if (mMode == Mode::MODE_SLIDESHOW) {
        currEvent.playback(mSendCamCallBack, true, 0.0f);
        usleep(static_cast<unsigned int>(mSlideShowFrameSec * 1000000.0f));        
        std::cerr << "SlideShow : interval:" << scene_rdl2::str_util::secStr(mSlideShowFrameSec) << '\n';
    } else {
        currEvent.playback(mSendCamCallBack, true, 0.0f);
    }
}

void
CamPlayback::editInterval(const float intervalSec)
{
    CamPlaybackEvent& currEvent = mEvent[mPlayCurrEventId];
    currEvent.setIntervalSec(intervalSec);
}

bool
CamPlayback::save(const std::string& filename, std::string& error) const
{
    error.clear();

    std::ofstream fout(filename, std::ios::trunc | std::ios::binary);
    if (!fout) {
        std::ostringstream ostr;
        ostr << "Can not create file. filename:" << filename;
        error = ostr.str();
        return false;
    }

    std::string data;
    scene_rdl2::rdl2::ValueContainerEnq enq(&data);

    enq.enq<float>(mRecInterval);
    enq.enqVLSizeT(mEvent.size());
    for (size_t i = 0; i < mEvent.size(); ++i) {
        mEvent[i].encode(enq);
    }
    enq.finalize();

    fout.write(data.data(), data.size());
    fout.close();

    return true;
}

bool
CamPlayback::load(const std::string& filename, std::string& error)
{
    auto fileRead = [](const std::string& filename, std::string& data, std::string& error) -> bool {
        auto getFileSize = [](const std::string& filename) -> size_t {
            struct stat buf;
            if (::stat(filename.c_str(), &buf) == 0) {
                return buf.st_size;
            }
            return 0;
        };

        size_t fileSize = getFileSize(filename);
        if (!fileSize) {
            std::ostringstream ostr;
            ostr << "Could not get fileSize. filename:" << filename;
            error = ostr.str();
            return false;
        }
        data.resize(fileSize);

        std::ifstream fin(filename, std::ios::in | std::ios::binary);
        if (!fin) {
            std::ostringstream ostr;
            ostr << "Could not open file. filename:" << filename;
            error = ostr.str();
            return false;
        }
        fin.read((char*)&data[0], fileSize);
        fin.close();
        return true;
    };

    error.clear();

    std::string data;
    if (!fileRead(filename, data, error)) {
        return false;
    }

    scene_rdl2::rdl2::ValueContainerDeq deq(data.data(), data.size());

    mMode = Mode::MODE_STOP;

    mRecInterval = deq.deq<float>();
    size_t size = deq.deqVLSizeT();
    mEvent.resize(size);
    for (size_t i = 0; i < size; ++i) {
        mEvent[i].decode(deq);
    }
    mStartEventId = 0;
    mEndEventId = mEvent.size() - 1;

    return true;
}

std::string
CamPlayback::show() const
{
    namespace str_util = scene_rdl2::str_util;

    auto showSec = [&](float sec) -> std::string {
        std::ostringstream ostr;
        ostr << sec << " (" << str_util::secStr(sec) << ")";
        return ostr.str();
    };

    auto getEventLengthSec = [&]() -> float {
        float sec = 0.0f;
        for (const auto& itr : mEvent) {
            sec += itr.getIntervalSec();
        }
        return sec;
    };

    std::ostringstream ostr;
    ostr << "CamPlayback {\n"
         << "  mMode:" << showMode(mMode) << '\n'
         << "  mRecInterval:" << showSec(mRecInterval) << '\n'
         << "  mEvent size:" << mEvent.size() << " (length: " << str_util::secStr(getEventLengthSec()) << ")\n"
         << "  mLoopPlayback:" << str_util::boolStr(mLoopPlayback) << '\n'
         << "  mReversePlayback:" << str_util::boolStr(mReversePlayback) << '\n'
         << "  mPlayCurrEventId:" << mPlayCurrEventId << '\n'
         << "  mStartEventId:" << mStartEventId << '\n'
         << "  mEndEventId:" << mEndEventId << '\n'
         << "  mPlayIntervalScale:" << mPlayIntervalScale << '\n'
         << "  mInitFrameSec:" << showSec(mInitFrameSec) << '\n'
         << "  mLastFrameSec:" << showSec(mLastFrameSec) << '\n'
         << "  mSlideShowFrameSec:" << showSec(mSlideShowFrameSec) << '\n'
         << "}";
    return ostr.str();
}

std::string
CamPlayback::showInterval() const
{
    namespace str_util = scene_rdl2::str_util;
    int w = str_util::getNumberOfDigits(static_cast<unsigned>(mEvent.size()));
    std::ostringstream ostr;
    ostr << "showInterval (size:" << mEvent.size() << ") {\n";
    for (size_t i = 0; i < mEvent.size(); ++i) {
        float currInterval = mEvent[i].getIntervalSec();
        ostr << " eventId:" << std::setw(w) << i
             << " interval:" << std::setw(6) << std::fixed << std::setprecision(5) << currInterval
             << " (" << str_util::secStr(currInterval) << ")\n";
    }
    ostr << "}";
    return ostr.str();
}

// static function
std::string
CamPlayback::showMode(const Mode& mode)
{
    switch (mode) {
    case Mode::MODE_STOP : return "MODE_STOP";
    case Mode::MODE_REC : return "MODE_REC";
    case Mode::MODE_PLAY : return "MODE_PLAY";
    case Mode::MODE_SLIDESHOW : return "MODE_SLIDESHOW";
    default : return "?";
    }
}

//------------------------------------------------------------------------------------------

bool
CamPlayback::isMakeNewEvent(const float interval) const
{
    if (mEvent.size() < 2) return true; // event is empty
    if (mRecInterval <= interval) return true; // it's enough interval to make next event
    if (mRecInterval <= mEvent.back().getIntervalSec()) return true; // between last and last - 1 has enough interval
    return false;
}

void
CamPlayback::addLast(const float intervalSec, const Mat4f& camMtx)
{
    size_t id = mEvent.size();
    mEvent.emplace_back(id, intervalSec, camMtx);
    mEndEventId = mEvent.size() - 1;
}

void
CamPlayback::replaceLast(const float intervalSec, const Mat4f& camMtx)
{
    mEvent.back().replace(intervalSec, camMtx);
}

void
CamPlayback::setEventRange(const size_t start, const size_t end)
{
    mStartEventId = start;
    mEndEventId = end;
    jumpTo(mStartEventId);
}

void
CamPlayback::resetEventRange()
{
    setEventRange(0, mEvent.size() - 1);
}

void
CamPlayback::processCurrPlaybackEvent()
{
    auto waitSec = [](const float sec) { usleep(static_cast<unsigned int>(sec * 1000000.0f)); };
    auto clampEventId = [&](const size_t id) -> size_t { return std::min(id, mEvent.size() - 1); };
    auto startId = [&]() -> size_t { return clampEventId((!mReversePlayback) ? mStartEventId : mEndEventId); };
    auto endId = [&]() -> size_t { return clampEventId((!mReversePlayback) ? mEndEventId : mStartEventId); };
    auto setNextEventId = [&]() {
        if (mReversePlayback) {
            if (mPlayCurrEventId == 0) {
                mPlayCurrEventId = mEvent.size();
            }
            mPlayCurrEventId--;
        } else {
            mPlayCurrEventId++;
            if (mEvent.size() <= mPlayCurrEventId) {
                mPlayCurrEventId = 0;
            }
        }
    };

    jumpTo(mPlayCurrEventId);

    if (mPlayCurrEventId == startId()) waitSec(mInitFrameSec);
    if (mPlayCurrEventId == endId()) waitSec(mLastFrameSec);

    if (mPlayCurrEventId == endId()) {
        std::cerr << "====>>>>> CamPlayback : last eventId:" << mPlayCurrEventId << " <<<<<====\n";

        if (mLoopPlayback) {
            mPlayCurrEventId = startId();
        } else {
            stop();
        }
    } else {
        setNextEventId();
    }
}

// static function
void
CamPlayback::threadMain(CamPlayback* camPlayback)
{
    // First of all change camPlayback's threadState condition and do notify_one to caller.
    camPlayback->mThreadState = ThreadState::IDLE;
    camPlayback->mCvBoot.notify_one(); // notify to CamPlayback's constructor

    std::cerr << ">> CamPlayback.cc playback thread booted\n";

    while (true) {
        if (camPlayback->mThreadShutdown) {
            break;
        }

        if (camPlayback->mMode == Mode::MODE_PLAY ||
            camPlayback->mMode == Mode::MODE_SLIDESHOW) {
            camPlayback->mThreadState = ThreadState::BUSY;
            if (!camPlayback->mEvent.size()) {
                camPlayback->stop();
            } else {
                camPlayback->processCurrPlaybackEvent();
            }
            camPlayback->mThreadState = ThreadState::IDLE;
        } else {
            usleep(1000); // 1000us = 1ms : wake up every 1ms and check camPlayback mode
        }
    }

    std::cerr << ">> CamPlayback.cc playback thread shutdown\n";
}

void
CamPlayback::parserConfigure()
{
    namespace str_util = scene_rdl2::str_util;

    mParser.description("cam playback command");
    mParser.opt("show", "", "show current information",
                [&](Arg& arg) -> bool { return arg.msg(show() + '\n'); });
    mParser.opt("showInterval", "", "show current camera event's all interval",
                [&](Arg& arg) -> bool { return arg.msg(showInterval() + '\n'); });
    mParser.opt("clear", "", "clear all event data",
                [&](Arg& arg) -> bool { clear(); return arg.msg("CLEAR\n"); });
    mParser.opt("stop", "", "stop cam play or rec",
                [&](Arg& arg) -> bool { stop(); return arg.msg("STOP\n"); });
    mParser.opt("recInterval", "<sec>", "cam rec interval",
                [&](Arg& arg) -> bool {
                    mRecInterval = (arg++).as<float>(0);
                    return arg.fmtMsg("recInterval:%s\n", str_util::secStr(mRecInterval).c_str());
                });
    mParser.opt("rec", "", "start rec",
                [&](Arg& arg) -> bool { recStart(); return arg.msg("REC\n"); });
    mParser.opt("recAdd", "<interval-sec>", "add current cam matrix to the end",
                [&](Arg& arg) -> bool {
                    float intervalSec = (arg++).as<float>(0);
                    recAdd(intervalSec);
                    return arg.fmtMsg("REC-ADD eventId:%d interval:%s\n",
                                      mEvent.size() - 1,
                                      str_util::secStr(intervalSec).c_str());
                });
    mParser.opt("initFrameLength", "<sec>", "set inital frame length",
                [&](Arg& arg) -> bool {
                    mInitFrameSec = (arg++).as<float>(0);
                    return arg.fmtMsg("initFrameSec:%s\n", str_util::secStr(mInitFrameSec).c_str());
                });
    mParser.opt("lastFrameLength", "<sec>", "set last frame length",
                [&](Arg& arg) -> bool {
                    mLastFrameSec = (arg++).as<float>(0);
                    return arg.fmtMsg("lastFrameSec:%s\n", str_util::secStr(mLastFrameSec).c_str());
                });
    mParser.opt("eventRange", "<startEventId> <endEventId>", "set playback start and end eventId",
                [&](Arg& arg) -> bool {
                    setEventRange(arg.as<size_t>(0), arg.as<size_t>(1));
                    arg += 2;
                    return arg.fmtMsg("eventRange start:%d end:%d\n", mStartEventId, mEndEventId);
                });
    mParser.opt("eventRangeReset", "", "reset to entire event",
                [&](Arg& arg) -> bool { resetEventRange(); return arg.msg("Reset eventRange\n"); });
    mParser.opt("jumpTo", "<eventId>", "set current eventId",
                [&](Arg& arg) -> bool {
                    jumpTo((arg++).as<size_t>(0));
                    return arg.fmtMsg("jumpTo:%d\n", mPlayCurrEventId);
                });
    mParser.opt("editInterval", "<interval-sec>", "edit interval of current eventId",
                [&](Arg& arg) -> bool {
                    float intervalSec = (arg++).as<float>(0);
                    editInterval(intervalSec);
                    return arg.fmtMsg("edit current interval %s\n", str_util::secStr(intervalSec).c_str());
                });
    mParser.opt("play", "", "start playback from beginning",
                [&](Arg& arg) -> bool { playStart(); return arg.msg("PLAY\n"); });
    mParser.opt("playSpeed", "<scale>", "set playback speed scale",
                [&](Arg& arg) -> bool {
                    mPlayIntervalScale = (arg++).as<float>(0);
                    return arg.fmtMsg("playSpeed scale:%f\n", mPlayIntervalScale);
                });
    mParser.opt("continue", "", "start playback from current position",
                [&](Arg& arg) -> bool { playContinue(); return arg.msg("CONTINUE\n"); });
    mParser.opt("slideShow", "<interval-sec>", "playback like slideshow",
                [&](Arg& arg) -> bool {
                    float intervalSec = (arg++).as<float>(0);
                    slideShow(intervalSec);
                    return arg.fmtMsg("SLIDE-SHOW interval:%s\n", str_util::secStr(intervalSec).c_str());
                });
    mParser.opt("loop", "<on|off>", "setup loop playback condition (default off)",
                [&](Arg& arg) -> bool {
                    mLoopPlayback = (arg++).as<bool>(0);
                    return arg.fmtMsg("loop %s\n", str_util::boolStr(mLoopPlayback).c_str());
                });
    mParser.opt("reverse", "<on|off>", "setup reverse playback mode (default off)",
                [&](Arg& arg) -> bool {
                    mReversePlayback = (arg++).as<bool>(0);
                    return arg.fmtMsg("reverse %s\n", str_util::boolStr(mReversePlayback).c_str());
                });
    mParser.opt("save", "<filename>", "save cam playback data",
                [&](Arg& arg) -> bool {
                    std::string error;
                    bool flag = save((arg++)(), error);
                    return arg.msg(std::string("save ") + (flag ? "OK" : "NG ") + error + '\n');
                });
    mParser.opt("load", "<filename>", "load cam playback data",
                [&](Arg& arg) -> bool {
                    std::string error;
                    bool flag = load((arg++)(), error);
                    return arg.msg(std::string("load ") + (flag ? "OK" : "NG ") + error + '\n');
                });
}

} // namespace arras_render
