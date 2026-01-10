// Copyright 2023-2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "ImageView.h"
#include "NavigationCam.h"

#ifdef __ARM_NEON__
// This works around OIIO including x86 based headers due to detection of SSE
// support due to sse2neon.h being included elsewhere
#define __IMMINTRIN_H
#define __NMMINTRIN_H
#define OIIO_NO_SSE 1
#define OIIO_NO_AVX 1
#define OIIO_NO_AVX2 1
#endif

#include <algorithm> // std::find
#include <cmath>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <vector>

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp> //split

#include <OpenImageIO/color.h>
#include <OpenImageIO/imagebufalgo.h>

#include <QString>
#include <QColor>
#include <QColorDialog>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>

#include <scene_rdl2/common/math/Color.h>
#include <scene_rdl2/common/math/Mat4.h>
#include <scene_rdl2/scene/rdl2/BinaryReader.h>
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
#include <mcrt_dataio/client/receiver/ClientReceiverConsoleDriver.h>
#include <mcrt_dataio/engine/mcrt/McrtControl.h>

#include <mcrt_messages/RDLMessage.h>
#include <mcrt_messages/RenderMessages.h>
#include <mcrt_messages/JSONMessage.h>
#include <mcrt_messages/CreditUpdate.h>
#include <mcrt_messages/GenericMessage.h>

#include "encodingUtil.h"
#include "outputRate.h"
#include "TelemetryPanelUtil.h"

//#define DEBUG_MSG_DISPLAY_FRAME
//#define DEBUG_MSG_POPULATE_RGB_FRAME

namespace {
constexpr int TARGET_WIDTH = 960;
constexpr int TARGET_HEIGHT = 600;
constexpr int OVERLAY_X_OFFSET = 50;
constexpr int OVERLAY_Y_OFFSET = 50;
constexpr int SCROLL_PAD = 16;
const std::string BEAUTY_PASS = "*beauty*";
const std::string PIXINFO_PASS = "*pixInfo*";
const std::string HEATMAP_PASS = "*heatMap*";
const std::string WEIGHT_PASS = "*weight*";
const std::string BEAUTYODD_PASS = "*beautyAux*";
const std::string COLOR_ICON = "/usr/share/icons/crystal_project/22x22/apps/colors.png";
const std::string WINDOW_ICON = ":/window-icon.png";

} // anon namespace

ImageView::ImageView(std::shared_ptr<mcrt_dataio::ClientReceiverFb> pFbReceiver,
                     std::unique_ptr<scene_rdl2::rdl2::SceneContext> sceneCtx,
                     bool overlay,
                     const std::string& overlayFontName,
                     int overlayFontSize,
                     const std::string& sessionName,
                     unsigned short numMcrtComps,
                     unsigned short numMcrtCompsMax,
                     unsigned aovInterval,
                     const std::string& scriptName,
                     bool exitScriptDone,
                     std::chrono::steady_clock::duration minUpdateInterval,
                     bool noInitialScale,
                     const std::chrono::time_point<std::chrono::steady_clock>& renderStart,
                     QWidget* parent)
    : QWidget(parent)
    , mRenderProgress(0.0)
    , mRenderInstance(0)
    , mOverlay(overlay)
    , mSessionName(sessionName)
    , mNumMcrtComps(numMcrtComps)
    , mNumMcrtCompsMax(numMcrtCompsMax)
    , mFontSize(overlayFontSize)
    , mImage(new QLabel(this))
    , mScrollArea(new QScrollArea(this))
    , mMainLayout(new QVBoxLayout)
    , mButtonRow(new QGroupBox)
    , mButtonLayout(new QHBoxLayout)
    , mButStart(new QPushButton(this))
    , mButStop(new QPushButton(this))
    , mButPause(new QPushButton(this))
    , mButPrevOutput(new QPushButton(this))
    , mButNextOutput(new QPushButton(this))
    , mButRunScript(new QPushButton(this))
    , mCboOutputs(new QComboBox(this))
    , mCboLights(new QComboBox(this))
    , mScaleCombo(new QComboBox(this))
    , mButColor(nullptr)
    , mFont(nullptr)
    , mFontColor(nullptr)
    , mFbReceiver(pFbReceiver)
    , mSceneCtx(std::move(sceneCtx))
    , mAovInterval(aovInterval)
    , mCurLight(nullptr)
    , mBlankDisplay(false)
    , mOutputNames({BEAUTY_PASS, PIXINFO_PASS, HEATMAP_PASS, WEIGHT_PASS, BEAUTYODD_PASS})
    , mNumBuiltinPasses(static_cast<unsigned int>(mOutputNames.size()))
    , mCurrentOutput(BEAUTY_PASS)
    , mRenderStart(renderStart)
    , mPaused(false)
    , mReceivedFirstFrame(false)
    , mNumChannels(0) // only used by non-progressive renders
    , mImgScale(1)
    , mOverlayXOffset(OVERLAY_X_OFFSET)
    , mOverlayYOffset(OVERLAY_Y_OFFSET)
    , mMinUpdateInterval(minUpdateInterval)
{
    std::ostringstream title;
    title << "Arras Render: ";
    if (!mSessionName.empty()) {
        title << mSessionName;
    }

    if (mNumMcrtComps > 0) {
        if (mNumMcrtComps == mNumMcrtCompsMax) {
            title << "mcrt(" << mNumMcrtComps << ")";
        } else {
            title << "mcrt(" << mNumMcrtComps << ":" << mNumMcrtCompsMax << ")";
        }
    }

    setWindowTitle(QString::fromStdString(title.str()));
    setWindowIcon(QIcon(QString::fromStdString(WINDOW_ICON)));

    const auto& sceneVars = mSceneCtx->getSceneVariables();
    mImgWidth = sceneVars.getRezedWidth();
    mImgHeight = sceneVars.getRezedHeight();

    // update telemetry overlay resolution as the same as image reso
    mFbReceiver->setTelemetryOverlayReso(mImgWidth, mImgHeight);

    // choose an initial scale to keep the image a reasonable size
    if (mImgWidth > TARGET_WIDTH  && !noInitialScale) {
        mImgScale = static_cast<unsigned int>(ceil(static_cast<float>(mImgWidth)/TARGET_WIDTH));
    }

    if ((mImgHeight/mImgScale) > TARGET_HEIGHT && !noInitialScale) {
        mImgScale = static_cast<unsigned int>(ceil(static_cast<float>(mImgHeight)/TARGET_HEIGHT));
    }

    const unsigned int width = mImgWidth / mImgScale;
    const unsigned int height = mImgHeight / mImgScale;
    mImage->setFixedSize(width, height);

    // Update telemetry overlay resolution for zoom action.
    // (but needs more future work and is currently skipped)
    // mFbReceiver->setTelemetryOverlayReso(width, height);

    // put the the image in a scrollable area
    mScrollArea->setWidget(mImage.get());
    mScrollArea->setMaximumSize(width+SCROLL_PAD, height+SCROLL_PAD);
    mScrollArea->resize(width, height);

    if (mOverlay) {
        mOverlayFontName = overlayFontName;
        mFont.reset(new QFont(QString::fromStdString(mOverlayFontName),
                              mFontSize));
        mFontColor.reset(new QPen(Qt::white));
    }

    mButtonRow->setFlat(true);

    mButStart->setText("Start Render");
    mButStop->setText("Stop Render");
    mButPause->setText("Pause Render");

    mButPrevOutput->setText("Prev Output");
    mButNextOutput->setText("Next Output");
    mButRunScript->setText("Run Script");
    mCboOutputs->addItem(QString::fromStdString(BEAUTY_PASS));

    initLights();

    mScaleCombo->addItem(QString::fromStdString("Scale 100%"), QString::fromStdString("Scale 100%"));
    mScaleCombo->addItem(QString::fromStdString("Scale 50%"), QString::fromStdString("Scale 50%"));
    mScaleCombo->addItem(QString::fromStdString("Scale 33%"), QString::fromStdString("Scale 33%"));
    mScaleCombo->addItem(QString::fromStdString("Scale 25%"), QString::fromStdString("Scale 25%"));
    mScaleCombo->addItem(QString::fromStdString("Scale 20%"), QString::fromStdString("Scale 20%"));
    mScaleCombo->setCurrentIndex(mImgScale-1);

    QIcon colorIcon(QString::fromStdString(COLOR_ICON));
    mButColor.reset(new QPushButton(colorIcon, "", this));

    mButtonLayout->addWidget(mButStart.get());
    mButtonLayout->addWidget(mButStop.get());
    mButtonLayout->addWidget(mButPause.get());
    mButtonLayout->addWidget(mButPrevOutput.get());
    mButtonLayout->addWidget(mButNextOutput.get());
    mButtonLayout->addWidget(mButRunScript.get());
    mButtonLayout->addWidget(mCboOutputs.get(), 1);
    mButtonLayout->addWidget(mCboLights.get(), 1);
    mButtonLayout->addWidget(mScaleCombo.get(), 1);
    mButtonLayout->addWidget(mButColor.get());

    mButtonRow->setLayout(mButtonLayout.get());

    mMainLayout->addWidget(mScrollArea.get());
    mMainLayout->addWidget(mButtonRow.get());

    QSize buttonSize = mButtonRow->sizeHint();
    setMaximumSize(width+40, height+buttonSize.height()+32);
    resize(width+40, height+buttonSize.height()+32);

    if (parent != nullptr) {
        parent->setLayout(mMainLayout.get());
    } else {
        setLayout(mMainLayout.get());
    }
    
    initCam();
    initImage();

    mFbReceiver->setTelemetryPanelPathVisClientInfoCallBack([&]() -> std::string {
        return pathVisClientInfoCallBack();
    });

    // connections need to be queued for for things which will be done from scripts since the
    // script runs in another thread a QueuedConnection makes this thread safe
    connect(this, SIGNAL(displayFrameSignal()),
            this, SLOT(displayFrameSlot()), Qt::QueuedConnection);

    connect(mButStart.get(), SIGNAL(released()),
            this, SLOT(handleStart()), Qt::QueuedConnection);

    connect(mButStop.get(), SIGNAL(released()),
            this, SLOT(handleStop()), Qt::QueuedConnection);

    connect(mButPause.get(), SIGNAL(released()),
            this, SLOT(handlePause()), Qt::QueuedConnection);

    connect(mButPrevOutput.get(), SIGNAL(released()),
            this, SLOT(handlePrevOutput()), Qt::QueuedConnection);

    connect(mButNextOutput.get(), SIGNAL(released()),
            this, SLOT(handleNextOutput()), Qt::QueuedConnection);

    connect(mButRunScript.get(), SIGNAL(released()),
            this, SLOT(handleRunScript()));

    connect(mCboOutputs.get(), SIGNAL(activated(int)),
            this, SLOT(handleAovSelect(int)));
    connect(mCboLights.get(), SIGNAL(activated(int)),
            this, SLOT(handleLightSelect(int)), Qt::QueuedConnection);
    connect(mScaleCombo.get(), SIGNAL(activated(int)),
            this, SLOT(handleScaleSelect(int)), Qt::QueuedConnection);

    connect(mButColor.get(), SIGNAL(released()),
            this, SLOT(handleColorButton()));

    connect(this, SIGNAL(setNewColorSignal(float,float,float)),
            this, SLOT(handleNewColor(float,float,float)), Qt::QueuedConnection);

    connect(this, SIGNAL(sendCredit(int)),
            this, SLOT(handleSendCredit(int)), Qt::QueuedConnection);

    connect(this, SIGNAL(exitProgramSignal()),
            this, SLOT(handleExitProgram()), Qt::QueuedConnection);

    connect(this, SIGNAL(statusOverlaySignal(short, QString)),
            this, SLOT(handleStatusOverlay(short, QString)), Qt::QueuedConnection);

    if (!scriptName.empty()) {
        // set up the scripting environment
        mScripting.init(this, scriptName, exitScriptDone);
        mScripting.scriptableButton("startButton", mButStart);
        mScripting.scriptableButton("stopButton", mButStop);
        mScripting.scriptableButton("pauseButton", mButPause);
        mScripting.scriptableButton("prevOutputButton", mButPrevOutput);
        mScripting.scriptableButton("nextOutputButton", mButNextOutput);
        mScripting.scriptableComboBox("lightSelector", mCboLights);
        mScripting.scriptableComboBox("aovSelector", mCboLights);
    } else {
        mButRunScript->setDisabled(true);
    }

    parserConfigure();
}

void
ImageView::setup(std::shared_ptr<arras4::sdk::SDK>& sdk)
{
    mSdk = sdk;
}

ImageView::~ImageView()
{
    // these would get destroyed automatically but destroy them
    // manually to control the order they're destroyed.
    mSdk.reset();
    mImage.reset();
    mScrollArea.reset();

    mButStart.reset();
    mButStop.reset();
    mButPause.reset();
    mButPrevOutput.reset();
    mButNextOutput.reset();
    mButRunScript.reset();
    mCboOutputs.reset();
    mCboLights.reset();
    mScaleCombo.reset();
    mButColor.reset();
    mButtonLayout.reset();
    mButtonRow.reset();

    mMainLayout.reset();
}

void
ImageView::initImage()
{
    // Avoiding locking the mutex as this should only be
    // called from the constructor
    QImage image(mImgWidth, mImgHeight, QImage::Format_RGB888);
    image.fill(Qt::black);

    if (mOverlay) {
        addOverlay(image);
    }

    mImage->setPixmap(QPixmap::fromImage(image));
}

// this needs to do the exit through a signal and slot
// to allow another thread to generate the close operation
void
ImageView::exitProgram()
{
    Q_EMIT exitProgramSignal();
}

void
ImageView::displayFrame()
{
    std::lock_guard<std::mutex> guard(mFrameMux);

    // ignore frames for the previous render
    // This appears to be broken ARRAS-3305
    //    if (mFbReceiver->getFrameId() < mRenderInstance) {
    //        std::cout << "Ignoring this frame because it's for an old render\n";
    //        return;
    //    }
#ifdef DEBUG_MSG_DISPLAY_FRAME
    std::cerr << ">> ImageView.cc displayFrame() passA\n";
#endif // end DEBUG_MSG_DISPLAY_FRAME
    populateRGBFrame();
#ifdef DEBUG_MSG_DISPLAY_FRAME
    std::cerr << ">> ImageView.cc displayFrame() passB\n";
#endif // end DEBUG_MSG_DISPLAY_FRAME
    // Check to see if we received any new outputs (aka AOVs aka buffers)
    // in the first frame we will receive an initial list of outputs,
    // if the client is using AOV Output Rate Control then later frames
    // may contain additional outputs.
    // http://mydw.anim.dreamworks.com/display/RD/AOV+Output+Rate+Control
    const auto numNewOutputs = mFbReceiver->getTotalRenderOutput();
    if (numNewOutputs > mOutputNames.size() - mNumBuiltinPasses) {
        std::cout << "Received " << numNewOutputs << " new outputs" << std::endl;
        for (size_t i = mOutputNames.size() - mNumBuiltinPasses; i < numNewOutputs; ++i) {
            std::string output(mFbReceiver->getRenderOutputName(static_cast<unsigned int>(i)));
            mOutputNames.push_back(output);
            std::cout << "\t" << output << std::endl;
        }
    }

    if (!mReceivedFirstFrame) {
        mReceivedFirstFrame = true;
        /*
        //
        // We don't need to update the image reso here.Because these values have been already set up
        // at construction time by sceneVariables. However, we have to reconsider the resolution change
        // during sessions and not support it at this moment. This is a future task.
        //
        mImgWidth = mFbReceiver->getWidth();
        mImgHeight = mFbReceiver->getHeight();
        */
        std::cerr << ">> ImageView.cc displayFrame() FirstFrame mImgWidth:" << mImgWidth << " mImgHeight:" << mImgHeight << '\n';
    }

    Q_EMIT displayFrameSignal();
}

void
ImageView::setInitialCondition()
{
    mRgbFrame.clear();
}

void
ImageView::clearDisplayFrame()
{
    mBlankDisplay = true;
    mRenderProgress = 0.0f;
    mRenderStart = std::chrono::steady_clock::now();
    displayFrame();
    mBlankDisplay = false;

    setInitialCondition(); // This makes the rgbFrame condition as very beginning of the process.
}

arras_render::NavigationCam*
ImageView::getNavigationCam()
{
    return ((mActiveCameraType == arras_render::CameraType::ORBIT_CAM) ?
            static_cast<arras_render::NavigationCam*>(&mOrbitCam) :
            static_cast<arras_render::NavigationCam*>(&mFreeCam));
}

bool
ImageView::processKeyboardEvent(const arras_render::KeyEvent* event)
{
    auto setDenoiseCondition = [&](const bool flag) {
        if (flag) {
            getFbReceiver()->setBeautyDenoiseMode(mcrt_dataio::ClientReceiverFb::DenoiseMode::ENABLE);
        } else {
            getFbReceiver()->setBeautyDenoiseMode(mcrt_dataio::ClientReceiverFb::DenoiseMode::DISABLE);
        }
    };

    bool used = false;

    if (event->getPress() == arras_render::KeyAction_Press) {
        if (event->getModifiers() == arras_render::QT_NoModifier) {
            used = true;
            switch (event->getKey()) {
            //
            // camera toggle
            //
            case arras_render::Key_O : // camera type toggle
                if (mActiveCameraType == arras_render::CameraType::ORBIT_CAM) {
                    // switch from orbit cam to free cam 
                    const auto xform = mOrbitCam.update(0.0f);
                    mOrbitCam.clearMovementState();
                    mFreeCam.resetTransform(xform, false);
                    mActiveCameraType = arras_render::CameraType::FREE_CAM;
                    std::cout << "===>>> Using FreeCam mode <<<===" << std::endl;
                } else {
                    // switch from free cam to orbit cam
                    const auto xform = mFreeCam.update(0.0f);
                    mFreeCam.clearMovementState();
                    mOrbitCam.resetTransform(xform, false);
                    mActiveCameraType = arras_render::CameraType::ORBIT_CAM;
                    std::cout << "===>>> Using OrbitCam mode <<<===" << std::endl;
                }
                break;

            //
            // Telemetry Control
            //
            case arras_render::Key_H : // telemetry overlay condition toggle
                mTelemetryOverlay = !mTelemetryOverlay;
                mFbReceiver->setTelemetryOverlayActive(mTelemetryOverlay);
                break;
            case arras_render::Key_SQUAREBRACKET_OPEN : // telemetry panel to parent
                if (mFbReceiver->getTelemetryOverlayActive()) mFbReceiver->switchTelemetryPanelToParent();
                break;
            case arras_render::Key_G : // telemetry panel to next
            case arras_render::Key_APOSTROPHE :  // telemetry panel to next
                if (mFbReceiver->getTelemetryOverlayActive()) mFbReceiver->switchTelemetryPanelToNext();
                break;
            case arras_render::Key_SEMICOLON : // telemetry panel to prev
                if (mFbReceiver->getTelemetryOverlayActive()) mFbReceiver->switchTelemetryPanelToPrev();
                break;
            case arras_render::Key_SLASH : // telemetry panel to child
                if (mFbReceiver->getTelemetryOverlayActive()) mFbReceiver->switchTelemetryPanelToChild();
                break;

            //
            // Denoise Control
            //
            case arras_render::Key_N : // denoise condition toggle
                mDenoise = !mDenoise;
                setDenoiseCondition(mDenoise);
                break;

            default : used = false; break;
            }
        } else if (event->getModifiers() == arras_render::QT_SHIFT) {
            used = true;
            switch (event->getKey()) {
            case arras_render::Key_G : // telemetry panel to prev
                if (mFbReceiver->getTelemetryOverlayActive()) mFbReceiver->switchTelemetryPanelToPrev();
                break;
            default : used = false; break;
            }
        }
    }

    return used;
}

bool
ImageView::telemetryPanelKeyboardEvent(const arras_render::KeyEvent* event, bool& activeKey)
{
    std::string telemetryPanelName = mFbReceiver->getCurrentTelemetryPanelName();
    if (telemetryPanelName == "pathVis") {
        return telemetryPanelPathVisKeyboardEvent(event, activeKey);
    }
    activeKey = false;
    return false;
}

bool
ImageView::telemetryPanelPathVisKeyboardEvent(const arras_render::KeyEvent* event, bool& activeKey)
{
    auto evalCmd = [&](const std::string& cmd) {
        std::string outMsg;
        if (evalArrasRenderCmd(cmd, outMsg)) std::cerr << outMsg << '\n';
    };
    auto startSimCmd = [&]() {
        evalCmd("mcrt rankAll");
        evalCmd("mcrt cmd renderContext pathVisMgr pathVis startSim");
        // mPathVisCamCheckpoint.updatePathVisCamMtx(getNavigationCam()->update(0.0f));
    };

    auto keyEventPathVisToggle = [&]() {
        mPathVisEnable = !mPathVisEnable;
        evalCmd("mcrt rankAll");
        if (mPathVisEnable) evalCmd("mcrt -cmd -pathVisMode on");
        else evalCmd("mcrt cmd pathVisMode off");
    };
    auto keyEventDeltaPix = [&](const bool x, const int delta) {
        evalCmd("mcrt rankAll");
        std::ostringstream ostr;
        ostr << "mcrt cmd renderContext pathVisMgr param " << (x ? "deltaPixelX " : "deltaPixelY ") << delta;
        evalCmd(ostr.str());
        startSimCmd();
    };
    auto keyEventSample = [&](const std::string& cmd, const int delta) {
        evalCmd("mcrt rankAll");
        std::ostringstream ostr;
        ostr << "mcrt cmd renderContext pathVisMgr param " << cmd << ' ' << delta;
        evalCmd(ostr.str());
        startSimCmd();
    };
    auto keyEventToggle = [&](const std::string& cmd) {
        evalCmd("mcrt rankAll");
        std::ostringstream ostr;
        ostr << "mcrt cmd renderContext pathVisMgr param " << cmd;
        evalCmd(ostr.str());
        startSimCmd();
    };
    auto keyEventActiveCurrLineToggle = [&]() {
        evalCmd("clientReceiver vecPktMgr activeCurrLineToggle");
    };
    auto keyEventDeltaCurrRankId = [&](const int delta) {
        std::ostringstream ostr;
        ostr << "clientReceiver vecPktMgr deltaCurrRankId " << delta;
        evalCmd(ostr.str());
    };
    auto keyEventDeltaCurrLineId = [&](const int delta) {
        if (delta > 0) evalCmd("clientReceiver vecPktMgr currRank nextCurr");
        else           evalCmd("clientReceiver vecPktMgr currRank prevCurr");
    };
    auto keyEventActiveCurrPosToggle = [&]() {
        evalCmd("clientReceiver vecPktMgr currRank activeCurrPosToggle");
    };
    auto keyEventOnlyDrawCurrRank = [&]() {
        evalCmd("clientReceiver vecPktMgr onlyDrawCurrRankToggle");
    };
    auto keyEventCamCheckpointPush = [&]() {
        const scene_rdl2::math::Mat4f camXform = getNavigationCam()->update(0.0f);
        mFreeCam.resetTransform(camXform, true);
        mOrbitCam.resetTransform(camXform, true);
        mPathVisCamCheckpoint.push(camXform);
    };
    auto keyEventCamCheckpoint = [&](const int delta) {
        scene_rdl2::math::Mat4f camXform = (delta < 0) ? mPathVisCamCheckpoint.getPrev() : mPathVisCamCheckpoint.getNext(); 
        sendCamUpdateMain(camXform, true);
        mFreeCam.resetTransform(camXform, true);
        mOrbitCam.resetTransform(camXform, true);
    };
    auto keyEventDeltaMoveStep = [&](const int delta) {
        float f = static_cast<float>(mPosMoveStep);
        f = (delta > 0) ? f * 2.0f : f * 0.5f;
        if (f < 1.0f) mPosMoveStep = 1;
        else if (f > 512.0f) mPosMoveStep = 512;
        else mPosMoveStep = static_cast<int>(f);
    };
    auto keyEventPathVisSetInitCam = [&]() {
        evalCmd("mcrt rankAll");
        evalCmd("mcrt cmd renderContext pathVisMgrSetInitCam");
        startSimCmd();
        mPathVisCamCheckpoint.updatePathVisCamMtx(getNavigationCam()->update(0.0f));
    };
    auto keyEventPathVisCamMtxToggle = [&]() {
        scene_rdl2::math::Mat4f camXform = mPathVisCamCheckpoint.swapBetweenCurrAndPathVisCam();
        sendCamUpdateMain(camXform, true);
        mFreeCam.resetTransform(camXform, true);
        mOrbitCam.resetTransform(camXform, true);
    };
    auto keyEventDrawLineOnly = [&](const bool press) -> bool {
        if (event->getAutoRepeat()) return false; // We skip autoRepeat event
        if (press) {
            if (mPathVisLastEscKeyPress) {
                std::cerr << ">> ImageView.cc keyEventDrawLineOnlye press=true FALSE\n";
                return false;
            }
            evalCmd("mcrt rankAll");
            evalCmd("clientReceiver telemetry stack top curr layout lineDrawOnly on");
            mPathVisLastEscKeyPress = true;
        } else {
            if (!mPathVisLastEscKeyPress) return false;
            evalCmd("mcrt rankAll");
            evalCmd("clientReceiver telemetry stack top curr layout lineDrawOnly off");
            mPathVisLastEscKeyPress = false;
        }
        return true;
    };
    auto keyEventHotKeyHelpToggle = [&]() -> bool {
        if (event->getAutoRepeat()) return false; // We skip autoRepeat event
        if (!mPathVisLastQuestionKeyPress) {
            mPathVisLastQuestionKeyPress = true;
            evalCmd("clientReceiver telemetry stack top curr layout hotKeyHelp on");
        } else {
            mPathVisLastQuestionKeyPress = false;
            evalCmd("clientReceiver telemetry stack top curr layout hotKeyHelp off");
        }
        return true;
    };
        
    bool used = false;
    activeKey = false;
    if (event->getPress() == arras_render::KeyAction_Press) {
        if (event->getModifiers() == arras_render::QT_NoModifier) {
            used = true;
            activeKey = true;
            switch (event->getKey()) {
            case arras_render::Key_1 : keyEventPathVisToggle(); break; // pathVis on/off toggle
            case arras_render::Key_2 : keyEventSample("deltaPixelSamples", 1); break;
            case arras_render::Key_3 : keyEventSample("deltaLightSamples", 1); break;
            case arras_render::Key_4 : keyEventSample("deltaBsdfSamples", 1); break;
            case arras_render::Key_5 : keyEventSample("deltaMaxDepth", 1); break;

            case arras_render::Key_6 : keyEventToggle("toggleUseSceneSamples"); break;
            case arras_render::Key_7 : keyEventToggle("toggleOcclusionRays"); break;
            case arras_render::Key_8 : keyEventToggle("toggleSpecularRays"); break;
            case arras_render::Key_9 : keyEventToggle("toggleDiffuseRays"); break;
            case arras_render::Key_0 : keyEventToggle("toggleBsdfSamples"); break;
            case arras_render::Key_MINUS : keyEventToggle("toggleLightSamples"); break;

            case arras_render::Key_EQUAL : keyEventActiveCurrLineToggle(); break;
            case arras_render::Key_P : keyEventDeltaCurrRankId(1); break;
            case arras_render::Key_SQUAREBRACKET_CLOSE : keyEventDeltaCurrLineId(1); break;
            case arras_render::Key_BACKSLASH : keyEventActiveCurrPosToggle(); break;
            case arras_render::Key_J : orbitCamRecenterToCurrPos(false); break;
            case arras_render::Key_K : keyEventOnlyDrawCurrRank(); break;
            case arras_render::Key_M : keyEventCamCheckpointPush(); break;
            case arras_render::Key_B : keyEventPathVisCamMtxToggle(); break; 
            case arras_render::Key_V : keyEventCamCheckpoint(1); break;
                
            case arras_render::Key_X : keyEventDeltaPix(true, mPosMoveStep); break; // deltaPix X positive
            case arras_render::Key_Y : keyEventDeltaPix(false, mPosMoveStep); break; // deltaPix Y positive
            case arras_render::Key_Z : keyEventDeltaMoveStep(1); break;

            case arras_render::Key_ESC : used = keyEventDrawLineOnly(true); break; 

            default :
                std::cerr << "ImageView.cc not assigned keyEvent " << event->show() << '\n';
                used = false;
                activeKey = false;
                break;
            }

        } else if (event->getModifiers() == arras_render::QT_SHIFT) {
            used = true;
            activeKey = true;
            switch (event->getKey()) {
            case 0x40 : keyEventSample("deltaPixelSamples", -1); break;
            case 0x23 : keyEventSample("deltaLightSamples", -1); break;
            case 0x24 : keyEventSample("deltaBsdfSamples", -1); break;
            case 0x25 : keyEventSample("deltaMaxDepth", -1); break;

            case arras_render::Key_P : keyEventDeltaCurrRankId(-1); break;
            case 0x7d : keyEventDeltaCurrLineId(-1); break;

            case arras_render::Key_J : orbitCamRecenterToCurrPos(true); break;
            case arras_render::Key_M : keyEventPathVisSetInitCam(); break;
            case arras_render::Key_V : keyEventCamCheckpoint(-1); break;

            case arras_render::Key_X : keyEventDeltaPix(true, -mPosMoveStep); break; // deltaPix X negative
            case arras_render::Key_Y : keyEventDeltaPix(false, -mPosMoveStep); break; // deltaPix Y negative
            case arras_render::Key_Z : keyEventDeltaMoveStep(-1); break;

            case arras_render::Key_QUESTION : used = keyEventHotKeyHelpToggle(); break;

            default :
                std::cerr << "ImageView.cc not assigned QT_SHIFT + keyEvent " << event->show() << '\n';
                used = false;
                activeKey = false;
                break;
            }
        }
    } else if (event->getPress() == arras_render::KeyAction_Release) {
        if (event->getModifiers() == arras_render::QT_NoModifier) {
            used = true;
            activeKey = true;
            switch (event->getKey()) {
            case arras_render::Key_ESC : used = keyEventDrawLineOnly(false); break; 
            default :
                used = false;
                activeKey = false;
                break;
            }
        }
    }
    return used;
}

bool
ImageView::telemetryPanelMousePressEvent(const arras_render::MouseEvent& event)
{
    std::string telemetryPanelName = mFbReceiver->getCurrentTelemetryPanelName();
    if (telemetryPanelName == "pathVis") {
        return telemetryPanelPathVisMousePressEvent(event);
    }

    return false;
}

bool
ImageView::telemetryPanelPathVisMousePressEvent(const arras_render::MouseEvent& event)
{
    auto calcImagePixPos = [&](const int labelX, const int labelY, int& imgX, int& imgY) {
        QPoint labelPos(labelX, labelY);

        if (!mImage->pixmap() || mImage->pixmap()->isNull()) return false;

        const qreal dpr = mImage->pixmap()->devicePixelRatio();
        const QSize pixMapLogicalSize = mImage->pixmap()->size() / dpr;
        const QRect cr = mImage->contentsRect();
        const QSize target = mImage->hasScaledContents() ? cr.size() : pixMapLogicalSize.scaled(cr.size(), Qt::KeepAspectRatio);
        const QRect ar = QStyle::alignedRect(mImage->layoutDirection(), mImage->alignment(), target, cr);
        if (!ar.contains(labelPos)) return false;

        const QPoint rel = labelPos - ar.topLeft();
        imgX = static_cast<int>(qRound(double(rel.x()) * pixMapLogicalSize.width() / ar.width()));
        imgY = mImage->height() - static_cast<int>(qRound(double(rel.y()) * pixMapLogicalSize.height() / ar.height())); // flip Y
        imgX = std::clamp(static_cast<int>(imgX * mImgScale), 0, static_cast<int>(pixMapLogicalSize.width() * mImgScale - 1));
        imgY = std::clamp(static_cast<int>(imgY * mImgScale), 0, static_cast<int>(pixMapLogicalSize.height() * mImgScale - 1));
        return true;
    };
    auto pickImagePixPos = [&](int& imgX, int& imgY) -> bool {
        if (!calcImagePixPos(event.getX(), event.getY(), imgX, imgY)) {
            return false; // early exit. picked outside image
        }
        std::cerr << "===>>> PickPos --- (x:" << event.getX() << " y:" << event.getY() << ") -> "
                  << "(imgX:" << imgX << " imgY:" << imgY << ") <<<===\n";
        return true;
    };
    auto evalCmd = [&](const std::string& cmd) {
        std::string outMsg;
        if (evalArrasRenderCmd(cmd, outMsg)) std::cerr << outMsg << '\n';
    };
    auto startSimCmd = [&]() {
        evalCmd("mcrt rankAll");
        evalCmd("mcrt cmd renderContext pathVisMgr pathVis startSim");
    };

    auto mousePressEventPickPos = [&]() -> bool {
        if (!mPathVisCamCheckpoint.isCurrCamPathVisCam()) return false; // currCam is not pathVisCam
        int imgX, imgY;
        if (!pickImagePixPos(imgX, imgY)) return false; // early exit. picked outside image
        evalCmd("mcrt rankAll");
        std::ostringstream ostr;
        ostr << "mcrt cmd renderContext pathVisMgr param pixel " << imgX << ' ' << imgY;
        evalCmd(ostr.str());
        startSimCmd();

        return true;
    };
    auto mousePressEventPickCurrent = [&]() -> bool {
        int imgX, imgY;
        if (!pickImagePixPos(imgX, imgY)) return false; // early exit. picked outside image
        std::ostringstream ostr;
        ostr << "clientReceiver vecPktMgr currRank pickCurr " << imgX << ' ' << imgY;
        std::cerr << ">> ImageView.cc mousePressEventPickCurrent : " << ostr.str() << '\n';
        evalCmd(ostr.str());
        return true;
    };

    // std::cerr << ">> ImageView.cc event:" << event.show() << '\n'; // for debug

    if (mPressShiftKey && !mPressAltKey && !mPressCtrlKey) { // SHIFT
        if (mousePressEventPickPos()) return true;
    } else if (!mPressShiftKey && !mPressAltKey && mPressCtrlKey) { // CTRL
        // std::cerr << ">> ImageView.cc mousePressEvent() CTRL\n"; // for debug
    } else if (mPressShiftKey && !mPressAltKey && mPressCtrlKey) { // SHIFT + CTRL
        if (mousePressEventPickCurrent()) return true;
    }
    return false;
}

bool
ImageView::telemetryPanelMouseReleaseEvent(const arras_render::MouseEvent& event)
{
    std::string telemetryPanelName = mFbReceiver->getCurrentTelemetryPanelName();
    if (telemetryPanelName == "pathVis") {
        return telemetryPanelPathVisMouseReleaseEvent(event);
    }

    return false;
}

bool
ImageView::telemetryPanelPathVisMouseReleaseEvent(const arras_render::MouseEvent& event)
{
    if (mPressShiftKey && !mPressAltKey && !mPressCtrlKey) { // SHIFT
        return true;
    } else if (!mPressShiftKey && !mPressAltKey && mPressCtrlKey) { // CTRL
        // std::cerr << ">> ImageView.cc mouseReleaseEvent() CTRL\n"; // for debug
    } else if (mPressShiftKey && !mPressAltKey && mPressCtrlKey) { // SHIFT + CTRL
        // std::cerr << ">> ImageView.cc mouseReleaseEvent() SHIFT+CTRL\n"; // for debug
    }
    return false;
}

bool
ImageView::evalArrasRenderCmd(const std::string& cmd, std::string& outMsg)
{
    scene_rdl2::grid_util::Parser& parser = mFbReceiver->consoleDriver().getRootParser();
    return parser.main(cmd, outMsg);
}

void
ImageView::orbitCamRecenterToCurrPos(const bool anim)
{
    using Vec3f = scene_rdl2::math::Vec3f;

    if (mActiveCameraType != arras_render::CameraType::ORBIT_CAM) {
        std::cerr << "Current camera is not ORBIT\n";
        return;
    }
    
    std::string outMsg;
    if (!evalArrasRenderCmd("clientReceiver vecPktMgr getCurrPosXYZ", outMsg)) {
        std::cerr << "failed to get currPos\n";
        return;
    }

    std::string flag;
    Vec3f vec;
    std::stringstream sstr(outMsg);
    sstr >> flag >> vec[0] >> vec[1] >> vec[2];
    if (flag != "t") {
        std::cerr << "no active currPos\n";
        return;
    }
    // std::cerr << "flag:" << flag << ' ' << vec << '\n'; // for debug

    if (!anim) {
        mOrbitCam.setCOI(vec);
        sendCamUpdate(1.0f);
    } else {
        const Vec3f delta = mOrbitCam.getCOI() - vec; 
        
        scene_rdl2::math::Mat4f camMtx;
        std::vector<scene_rdl2::math::Mat4f> camMtxTbl;

        const int max = mPathVisCamAnimSegmentTotal;
        const Vec3f deltaStep = delta / static_cast<float>(max);
        for (int i = max - 1 ; i >= 0; --i) {
            Vec3f currCOI = deltaStep * i + vec;
            mOrbitCam.setCOI(currCOI);
            camMtx = mOrbitCam.update(1.0f);
            camMtxTbl.push_back(camMtx);
        }

        mCamPlayback.clear();
        mCamPlayback.recCamTbl(camMtxTbl, 0.0f, true);
        mCamPlayback.quickPlayback();
    }
}

void
ImageView::populateRGBFrame()
{
    if (mBlankDisplay) {
#ifdef DEBUG_MSG_POPULATE_RGB_FRAME
        std::cerr << ">> ImageView.cc populateRGBFrame() before memset()\n";
#endif // end DEBUG_MSG_POPULATE_RGB_FRAME
        std::memset(&mRgbFrame[0], 0, mRgbFrame.size());
#ifdef DEBUG_MSG_POPULATE_RGB_FRAME
        std::cerr << ">> ImageView.cc populateRGBFrame() after memset()\n";
#endif // end DEBUG_MSG_POPULATE_RGB_FRAME
        return;
    }

    if (mCurrentOutput == BEAUTY_PASS) {
#ifdef DEBUG_MSG_POPULATE_RGB_FRAME
        std::cerr << ">> ImageView.cc populateRGBFrame() before getBeautyRgb888()\n";
#endif // end DEBUG_MSG_POPULATE_RGB_FRAME
        if (!mFbReceiver->getBeautyRgb888(mRgbFrame,
                                          true,
                                          false)) {
            std::cerr << "populateRGBFrame() failed. " << mFbReceiver->getErrorMsg() << '\n';
        }
#ifdef DEBUG_MSG_POPULATE_RGB_FRAME
        std::cerr << ">> ImageView.cc populateRGBFrame() after getBeautyRgb888()\n";
#endif // end DEBUG_MSG_POPULATE_RGB_FRAME
    } else if (mCurrentOutput == PIXINFO_PASS) {
        if (mFbReceiver->getPixelInfoStatus()) {
            mFbReceiver->getPixelInfoRgb888(mRgbFrame,
                                            true, // top2bottom
                                            false); // isSrgb
        }
    } else if (mCurrentOutput == HEATMAP_PASS) {
        if (mFbReceiver->getHeatMapStatus()) {
            mFbReceiver->getHeatMapRgb888(mRgbFrame,
                                          true, // top2bottom
                                          false); // isSrgb
        }
    } else if (mCurrentOutput == WEIGHT_PASS) {
        if (mFbReceiver->getWeightBufferStatus()) {
            mFbReceiver->getWeightBufferRgb888(mRgbFrame,
                                               true, // top2bottom
                                               false); // isSrgb
        }
    } else if (mCurrentOutput == BEAUTYODD_PASS) {
        if (mFbReceiver->getRenderBufferOddStatus()) {
            mFbReceiver->getBeautyAuxRgb888(mRgbFrame,
                                            true, // top2bottom
                                            false); // isSrgb
        }
    } else {
        std::cout << "Switching to " << mCurrentOutput
                  << " chans=" << mFbReceiver->getRenderOutputNumChan(mCurrentOutput)
                  << std::endl;

        mFbReceiver->getRenderOutputRgb888(mCurrentOutput, mRgbFrame, true);
    }

    mRenderProgress = mFbReceiver->getProgress() * 100;
}

bool
ImageView::savePPM(const std::string& filename) const
{
    std::cerr << ">> ImageView.cc savePPM(" << filename << ")\n"
              << "  mRgbFrame.size():" << mRgbFrame.size() << '\n'
              << "  mImgWidth:" << mImgWidth << '\n'
              << "  mImgHeight:" << mImgHeight << '\n'
              << "  expectedSize:" << mImgWidth * mImgHeight * 3 << '\n';

    auto getPix = [&](int u, int v, unsigned char c[3]) {
        const int offPix = v * mImgWidth + u;
        const int offset = offPix * 3;
        c[0] = mRgbFrame[offset];
        c[1] = mRgbFrame[offset + 1];
        c[2] = mRgbFrame[offset + 2];
    };

    constexpr int valReso = 256;

    std::ofstream ofs(filename);
    if (!ofs) return false;

    ofs << "P3\n" << mImgWidth << ' ' << mImgHeight << '\n' << (valReso - 1) << '\n';
    for (int v = (int)mImgHeight - 1; v >= 0; --v) {
        for (int u = 0; u < (int)mImgWidth; ++u) {
            unsigned char c[3];
            getPix(u, v, c);
            ofs << (int)c[0] << ' ' << (int)c[1] << ' ' << (int)c[2] << ' ';
        }
    }

    ofs.close();
    return true;
}

bool
ImageView::saveQImagePPM(const std::string& filename, const QImage& image) const
{
    const int width = image.width();
    const int height = image.height();

    std::cerr << ">> ImageView.cc saveQImagePPM(" << filename << ")\n"
              << "  width:" << width << " height:" << height << '\n';

    std::ofstream ofs(filename);
    if (!ofs) return false;

    constexpr int valReso = 256;

    ofs << "P3\n" << width << ' ' << height << '\n' << (valReso - 1) << '\n';
    for (int v = height - 1; v >= 0; --v) {
        for (int u = 0; u < width; ++u) {
            QRgb rgb = image.pixel(u, v);
            ofs << qRed(rgb) << ' ' << qGreen(rgb) << ' ' << qBlue(rgb) << ' ';
        }
    }

    ofs.close();
    return true;
}

void
ImageView::updateOutputsComboBox()
{
    for (int i = mCboOutputs->count(); i < static_cast<int>(mOutputNames.size()); ++i) {
        mCboOutputs->addItem(QString::fromStdString(mOutputNames[i]));
    }
}

void
ImageView::displayFrameSlot()
{
    std::lock_guard<std::mutex> guard(mFrameMux);

    if (mCboOutputs->count() != static_cast<int>(mOutputNames.size())) {
        updateOutputsComboBox();
    }

    if (mRgbFrame.size() > 0) {
        // We got issues and the following QImage construction does not work properly if the input image size
        // is 1667 x 757. Result QImage is not properly converted and the resulting image is 1 pixel shifted
        // every scanline. Looks like this is a bug in the QImage constructor.
        // One of the easiest fixes is using another constructor that has a per-line-size argument.
        // QImage image(mRgbFrame.data(), mImgWidth, mImgHeight, QImage::Format_RGB888); // <- does not work
        QImage image(mRgbFrame.data(), mImgWidth, mImgHeight, mImgWidth * 3, QImage::Format_RGB888);

        if (mOverlay) {
            addOverlay(image);
        }

        /* useful debug code
        static int iii = 0;
        iii++;
        if (iii == 24 * 5) { // every 24 * 5, save QImage as PPM format
            saveQImagePPM("tmpQImage.ppm", image); 
            iii = 0;
        }
        */

        QImage scaledImage = image.scaled(mImgWidth/mImgScale,mImgHeight/mImgScale);
        mImage->setPixmap(QPixmap::fromImage(scaledImage));
    } else {

        // there isn't an image yet so create a black one
        QImage image(mImgWidth, mImgHeight, QImage::Format_RGB888);
        image.fill(Qt::black);

        if (mOverlay) {
            addOverlay(image);
        }

        QImage scaledImage = image.scaled(mImgWidth/mImgScale, mImgHeight/mImgScale);
        mImage->setPixmap(QPixmap::fromImage(scaledImage));
    }
}

void
ImageView::addOverlay(QImage& image)
{
    QPainter qp(&image);
    qp.setPen(*mFontColor);
    qp.setFont(*mFont);

    const auto now = std::chrono::steady_clock::now();
    std::chrono::seconds durationSeconds(std::chrono::duration_cast<std::chrono::seconds>(now - mRenderStart));
    std::chrono::minutes durationMinutes(std::chrono::duration_cast<std::chrono::minutes>(durationSeconds));
    std::chrono::hours durationhours(std::chrono::duration_cast<std::chrono::hours>(durationMinutes));

    durationMinutes -= durationhours;
    durationSeconds -= durationMinutes;

    boost::format hmsPctFmt("%02d:%02d:%02d - %0.1f%%");
    hmsPctFmt % durationhours.count()
              % durationMinutes.count()
              % durationSeconds.count()
              % mRenderProgress;

    qp.drawText(mOverlayXOffset, mImgHeight - mOverlayYOffset, QString::fromStdString(hmsPctFmt.str()));
}

void
ImageView::setStatusOverlay(short index, std::string message)
{
    Q_EMIT statusOverlaySignal(index, QString(message.c_str()));
}

//------------------------------------------------------------------------------------------

bool
ImageView::sendCommand(const std::string &cmd,
                       const MsgCallBack& msgCallBack)
//
// for debug console
//
{
    if (cmd == "sendWholeScene") {
        scene_rdl2::rdl2::BinaryWriter w(*mSceneCtx);
        w.setDeltaEncoding(false);

        mcrt::RDLMessage::Ptr rdlMsg = std::make_shared<mcrt::RDLMessage>();
        w.toBytes(rdlMsg->mManifest, rdlMsg->mPayload);
        // rdlMsg->mForceReload = false;
        rdlMsg->mForceReload = true;

        mRenderProgress = 0.0;
        mRenderInstance = mRenderInstance + 1;
        rdlMsg->mSyncId = static_cast<int>(mRenderInstance);

        mSceneCtx->commitAllChanges();
        mSdk->sendMessage(rdlMsg);
        mRenderStart = std::chrono::steady_clock::now();

        if (!msgCallBack("sendWholeScene\n")) return false;
        
    } else if (cmd == "sendEmptyScene") {
        scene_rdl2::rdl2::BinaryWriter w(*mSceneCtx);
        w.setDeltaEncoding(true);

        mcrt::RDLMessage::Ptr rdlMsg = std::make_shared<mcrt::RDLMessage>();
        w.toBytes(rdlMsg->mManifest, rdlMsg->mPayload);
        rdlMsg->mForceReload = false;

        if (!msgCallBack(scene_rdl2::rdl2::BinaryReader::showManifest(rdlMsg->mManifest) + '\n')) return false;

        mRenderProgress = 0.0;
        mRenderInstance = mRenderInstance + 1;
        rdlMsg->mSyncId = static_cast<int>(mRenderInstance);

        mSceneCtx->commitAllChanges(); // just in case
        mSdk->sendMessage(rdlMsg);
        mRenderStart = std::chrono::steady_clock::now();

        if (!msgCallBack("sendEmptyScene\n")) return false;
    }

    return true;
}

void
ImageView::changeImageSize(int width, int height)
//
// for debug console
//
{
    mImgWidth = width;
    mImgHeight = height;

    // 
    // Probably we need more consideration for QT related display image logic.
    // The current implementation does not test well in terms of QT and needs more work.
    // This is just testing back-end engine functionality at this moment.
    //                                                   
    // We might consider about mImgScale here like follows.
    //
    //    if (mImgWidth > TARGET_WIDTH) {
    //        mImgScale = static_cast<unsigned int>(ceil(static_cast<float>(mImgWidth)/TARGET_WIDTH));
    //    }
    //    if ((mImgHeight/mImgScale) > TARGET_HEIGHT) {
    //        mImgScale = static_cast<unsigned int>(ceil(static_cast<float>(mImgHeight)/TARGET_HEIGHT));
    //    }
    //
    const unsigned w = mImgWidth / mImgScale;
    const unsigned h = mImgHeight / mImgScale;
    mImage->setFixedSize(w, h);

    // Update telemetry overlay resolution for zoom action.
    // (but needs more future work and is currently skipped)
    // mFbReceiver->setTelemetryOverlayReso(w, h);
    {
        scene_rdl2::rdl2::SceneVariables &sceneVars = mSceneCtx->getSceneVariables();
        scene_rdl2::rdl2::SceneVariables::UpdateGuard guard(&sceneVars);
        
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sImageWidth, width);
        sceneVars.set(scene_rdl2::rdl2::SceneVariables::sImageHeight, height);
    }
}

void
ImageView::changeROI(int xMin, int yMin, int xMax, int yMax)
{
    // 
    // Probably we need more consideration for QT related display image logic.
    // The current implementation does not test well in terms of QT and needs more work.
    // This is just testing back-end engine functionality at this moment.
    //                                                   
    scene_rdl2::rdl2::SceneVariables &sceneVars = mSceneCtx->getSceneVariables();
    scene_rdl2::rdl2::SceneVariables::UpdateGuard guard(&sceneVars);
    
    const std::vector<int> subViewport = {xMin, yMin, xMax, yMax};
    sceneVars.set(scene_rdl2::rdl2::SceneVariables::sSubViewport, subViewport);
}

void
ImageView::changeROIoff()
{
    scene_rdl2::rdl2::SceneVariables &sceneVars = mSceneCtx->getSceneVariables();
    sceneVars.disableSubViewport();
}

void
ImageView::setOverlayParam(unsigned offsetX, unsigned offsetY, unsigned fontSize)
{
    mOverlayXOffset = offsetX;
    mOverlayYOffset = offsetY;
    mFontSize = static_cast<int>(fontSize);
    mFont.reset(new QFont(QString::fromStdString(mOverlayFontName), mFontSize));
}

void
ImageView::getImageDisplayWidgetPos(int& topLeftX, int& topLeftY)
{
    const int baseX = this->x();
    const int baseY = this->y();

    const QRect scrollGeom = mScrollArea->frameGeometry();
    const int scrollBaseX = scrollGeom.left();
    const int scrollBaseY = scrollGeom.top();

    const QRect scrollRect = mScrollArea->childrenRect();
    const int t = scrollRect.top();
    
    topLeftX = baseX + scrollBaseX + 1;
    topLeftY = baseY + scrollBaseY + t + 37;
}

//------------------------------------------------------------------------------------------

void
ImageView::handleStartStop(bool start)
{
    std::lock_guard<std::mutex> guard(mSceneMux);
    mPaused = !start;

    const std::string msgDesc = start ? "Start" : "Stop";

    std::cout << "Sending Render " << msgDesc << " Message" << std::endl;
    mSdk->sendMessage((mcrt::RenderMessages::createControlMessage(!start)));
    mRenderStart = std::chrono::steady_clock::now();
}

void
ImageView::handleStart()
{
    return handleStartStop(true);
}

void
ImageView::handleStop()
{
    return handleStartStop(false);
}

void
ImageView::handlePause()
{
    std::lock_guard<std::mutex> guard(mSceneMux);
    mPaused = !mPaused;

    if (mPaused) {
        std::cout << "Pausing" << std::endl;
        mSdk->sendMessage((mcrt::RenderMessages::createControlMessage(true)));
    } else {
        std::cout << "Un-pausing" << std::endl;
        sendSceneUpdate(true);
    }
}

void
ImageView::initLights()
{
    for (auto itr = mSceneCtx->beginSceneObject(); itr != mSceneCtx->endSceneObject(); ++itr) {
        if (itr->second->getType() & scene_rdl2::rdl2::INTERFACE_LIGHT) {
            scene_rdl2::rdl2::Light* lgt = itr->second->asA<scene_rdl2::rdl2::Light>();

            if (lgt->get(scene_rdl2::rdl2::Light::sOnKey)) {
                std::string fullName(itr->first);

                std::list<std::string> parts;
                boost::split(parts, fullName, boost::is_any_of("/"));
                std::string shortName(parts.back());

                mCboLights->addItem(QString::fromStdString(shortName),
                                    QString::fromStdString(fullName));

                if (mCurLight == nullptr) {
                    mCurLight = lgt;
                }
            }
        }
    }
}

void
ImageView::initCam()
{
    // getActiveCamera helper cam only returns a const, but we can
    // use that to determine the name of the active camera
    const scene_rdl2::rdl2::Camera* constCam = mSceneCtx->getPrimaryCamera();
    mRdlCam = mSceneCtx->getSceneObject(constCam->getName())->asA<scene_rdl2::rdl2::Camera>();
    scene_rdl2::math::Mat4f camXform(mRdlCam->get(scene_rdl2::rdl2::Node::sNodeXformKey));
    const float near = mRdlCam->get(scene_rdl2::rdl2::Camera::sNearKey);

    mFreeCam.resetTransform(camXform, true);

    mOrbitCam.resetTransform(camXform, true);
    mOrbitCam.setNear(near);
    mOrbitCam.setCalcFocusPointCallBack([&]() -> scene_rdl2::math::Vec3f {
        std::string outMsg;
        scene_rdl2::grid_util::Parser& parser = getFbReceiver()->consoleDriver().getRootParser();
        if (parser.main("clientReceiver getOrbitCamAutoFocusPoint", outMsg)) {
            scene_rdl2::math::Vec3f vec;
            std::stringstream sstr(outMsg);
            sstr >> vec[0] >> vec[1] >> vec[2];
            if (sstr) return vec;
        }
        return scene_rdl2::math::Vec3f();
    });

    mPathVisCamCheckpoint.updatePathVisCamMtx(camXform);
    mPathVisCamCheckpoint.push(camXform);

    mCamPlayback.setSendCamCallBack
        ([&](const scene_rdl2::math::Mat4f& camMtx) { // void SendCamCallBack()
            sendCamUpdateMain(camMtx, true);            
        });
    mCamPlayback.setSendForceRenderStartCallBack
        ([&]() { // void SendForceRenderStartCallBack()
            std::string outMsg;
            evalArrasRenderCmd("genericMsg " + mcrt_dataio::McrtControl::msgGen_forceRenderStart(), outMsg);
        });
}

void
ImageView::changeRenderOutput(bool updateAovCombo)
{
    if (updateAovCombo) {
        int aovIndex = mCboOutputs->findText(QString::fromStdString(mCurrentOutput),
                                       static_cast<Qt::MatchFlags>(Qt::MatchExactly|Qt::MatchCaseSensitive));

        if (aovIndex != -1) {
            mCboOutputs->setCurrentIndex(aovIndex);
        }
    }

    if (mAovInterval > 0) {
        std::string priorityAov;
        if (mCurrentOutput != BEAUTY_PASS) {
            priorityAov = mCurrentOutput;
        }

        arras_render::setOutputRate(*mSdk, mAovInterval, 1, priorityAov, 1);
    }

    populateRGBFrame();

    std::cout << "Viewing\t" << mCurrentOutput << std::endl;
}

void
ImageView::handlePrevOutput()
{
    bool doDisplay = false;
    {
        std::lock_guard<std::mutex> guard(mFrameMux);
        if (mReceivedFirstFrame) {
            auto itr = std::find(mOutputNames.begin(), mOutputNames.end(), mCurrentOutput);
            if (itr != mOutputNames.end()) {
                if (itr == mOutputNames.begin()) {
                    itr = mOutputNames.end();
                }

                --itr;
                mCurrentOutput = *itr;

                changeRenderOutput();
                doDisplay = true;
            }
        }
    }

    if (doDisplay) Q_EMIT displayFrameSignal();
}

void
ImageView::handleNextOutput()
{
    bool doDisplay = false;
    {
        std::lock_guard<std::mutex> guard(mFrameMux);
        if (mReceivedFirstFrame) {
            auto itr = std::find(mOutputNames.begin(), mOutputNames.end(), mCurrentOutput);
            if (itr != mOutputNames.end()) {
                ++itr;
                if (itr == mOutputNames.end()) {
                    itr = mOutputNames.begin();
                }

                mCurrentOutput = *itr;

                changeRenderOutput();
                doDisplay = true;
            }
        }
    }

    if (doDisplay) Q_EMIT displayFrameSignal();
}

void
ImageView::handleStatusOverlay(short index, QString message)
{
    if (index >= 0) {
        if ((index + 1U) > mStatusOverlay.size()) {
            mStatusOverlay.resize(index + 1);
        }
        mStatusOverlay[index] = message.toStdString();
    } else {
        mStatusOverlay.clear();
    }

    // update the image
    displayFrameSlot();
}

void
ImageView::handleAovSelect(int index)
{
    bool doDisplay = false;
    const std::string bufferName = mCboOutputs->itemText(index).toStdString();
    {
        std::lock_guard<std::mutex> guard(mFrameMux);
        if (mReceivedFirstFrame && mCurrentOutput != bufferName) {
            mCurrentOutput = bufferName;

            changeRenderOutput(false);
            doDisplay = true;
        }
    }

    if (doDisplay) Q_EMIT displayFrameSignal();
}

void
ImageView::handleScaleSelect(int index)
{
    mImgScale = index + 1;

    const unsigned int width = mImgWidth / mImgScale;
    const unsigned int height = mImgHeight / mImgScale;
    mImage->setFixedSize(width, height);
    mScrollArea->setMaximumSize(width+SCROLL_PAD, height+SCROLL_PAD);
    const QSize buttonSize = mButtonRow->sizeHint();
    setMaximumSize(width+40, height+buttonSize.height()+32);

    // Update telemetry overlay resolution for zoom action.
    // (but needs more future work and is currently skipped)
    // mFbReceiver->setTelemetryOverlayReso(width, height);

    displayFrameSlot();
}

void
ImageView::handleLightSelect(int index)
{
    const std::string shortName = mCboLights->itemText(index).toStdString();
    const std::string fullName = mCboLights->itemData(index).toString().toStdString();
    {
        std::lock_guard<std::mutex> guard(mSceneMux);
        mCurLight = mSceneCtx->getSceneObject(fullName)->asA<scene_rdl2::rdl2::Light>();
    }

    std::cout << "Current light changed to: "
                << shortName
                << std::endl;
}

void
ImageView::handleExitProgram()
{
    close();
}

void
ImageView::handleNewColor(float red, float green, float blue)
{
    std::lock_guard<std::mutex> guard(mSceneMux);

    const scene_rdl2::math::Color newRdlColor(red, green, blue);

    std::cout << "New color " << newRdlColor << std::endl;

    mCurLight->beginUpdate();
    mCurLight->set(scene_rdl2::rdl2::Light::sColorKey, newRdlColor);
    mCurLight->endUpdate();
    sendSceneUpdate();
}

void
ImageView::handleColorButton()
{
    std::lock_guard<std::mutex> guard(mSceneMux);

    if (mCurLight != nullptr) {
        std::ostringstream title;
        title << "Color for: " << mCurLight->getName();

        const auto rdlColor = mCurLight->get(scene_rdl2::rdl2::Light::sColorKey);
        std::cout << "Current color " << rdlColor << std::endl;

        QColor curColor;
        curColor.setRgbF(rdlColor.r, rdlColor.g, rdlColor.b);

        const QColor newColor = QColorDialog::getColor(curColor,
                                                       this,
                                                       QString::fromStdString(title.str()));

        if (newColor.isValid()) {
            Q_EMIT setNewColorSignal(static_cast<float>(newColor.redF()),
                                     static_cast<float>(newColor.greenF()),
                                     static_cast<float>(newColor.blueF()));
        }
    }
}

void
ImageView::sendCamUpdate(float dt, bool forceUpdate)
{
    // dt <= 0 means use measured interval since last update
    scene_rdl2::math::Mat4f camMat;
    if (dt < 0) {
        if (mCameraUpdateTime.isInit()) {
            dt = 0.0f;
        } else {
            dt = mCameraUpdateTime.end();
        }
        mCameraUpdateTime.start();
    }

    camMat = getNavigationCam()->update(dt);
    if (mCamPlayback.getMode() == arras_render::CamPlayback::Mode::MODE_REC) {
        mCamPlayback.recCam(camMat);
    } else {
        mCamPlayback.saveCam(camMat); // save camera matrix only
    }

    sendCamUpdateMain(camMat, forceUpdate);
}

void
ImageView::sendCamUpdateMain(const scene_rdl2::math::Mat4f& camMat, const bool forceUpdate)
{
    mRdlCam->beginUpdate();
    mRdlCam->set(scene_rdl2::rdl2::Node::sNodeXformKey, scene_rdl2::math::toDouble(camMat));
    mRdlCam->endUpdate();
    sendSceneUpdate(forceUpdate);

    mPathVisCamCheckpoint.update(camMat);
}

void
ImageView::sendSceneUpdate(bool forceUpdate)
{
    // make sure we don't update too often
    if (!forceUpdate &&
        (mMinUpdateInterval > std::chrono::steady_clock::duration::zero())) {
        const auto now = std::chrono::steady_clock::now();
        std::chrono::steady_clock::duration dt = now - mRenderStart;
        if (dt < mMinUpdateInterval)
            return;
    }

    mPaused = false;
    scene_rdl2::rdl2::BinaryWriter w(*mSceneCtx);
    w.setDeltaEncoding(true);

    mcrt::RDLMessage::Ptr rdlMsg = std::make_shared<mcrt::RDLMessage>();
    w.toBytes(rdlMsg->mManifest, rdlMsg->mPayload);
    rdlMsg->mForceReload = false;

    mRenderProgress = 0.0;
    mRenderInstance = mRenderInstance + 1;
    rdlMsg->mSyncId = static_cast<int>(mRenderInstance);

    mSceneCtx->commitAllChanges();
    mSdk->sendMessage(rdlMsg);
    mRenderStart = std::chrono::steady_clock::now();
}

void
ImageView::handleSendCredit(int amount)
{
    std::cout << std::endl << "Sending credit: " << amount << std::endl;
    mcrt::CreditUpdate::Ptr creditMsg = std::make_shared<mcrt::CreditUpdate>();
    creditMsg->value() = amount;
    mSdk->sendMessage(creditMsg);
}

void
ImageView::handleRunScript()
{
    mScripting.handleRunScript();
}


void
ImageView::mousePressEvent(QMouseEvent *aMouseEvent)
{
    const QPoint posInLabel = mImage->mapFrom(this, aMouseEvent->pos());    
    arras_render::MouseEvent evt(posInLabel.x(), // mouse position X in the mImage QLabel
                                 posInLabel.y(), // mouse position Y in the mImage QLabel
                                 aMouseEvent->modifiers(),
                                 aMouseEvent->button(),
                                 aMouseEvent->buttons());

    if (telemetryPanelMousePressEvent(evt)) {
        std::cerr << ">> ImageView.cc telemetryPanelMousePressEvent TRUE\n";
    } else if (getNavigationCam()->processMousePressEvent(&evt)) {
        sendCamUpdate(-1);
    }
}

void
ImageView::mouseReleaseEvent(QMouseEvent *aMouseEvent)
{
    const QPoint posInLabel = mImage->mapFrom(this, aMouseEvent->pos());    
    arras_render::MouseEvent evt(posInLabel.x(), // mouse position X in the mImage QLabel
                                 posInLabel.y(), // mouse position Y in the mImage QLabel
                                 aMouseEvent->modifiers(),
                                 aMouseEvent->button(),
                                 aMouseEvent->buttons());

    if (telemetryPanelMouseReleaseEvent(evt)) {
        std::cerr << ">> ImageView.cc telemetryPanelMouseReleaseEvent TRUE\n";
    } else if (getNavigationCam()->processMouseReleaseEvent(&evt)) {
        sendCamUpdate(-1);
    }
}

void
ImageView::mouseMoveEvent(QMouseEvent *aMouseEvent)
{
    const arras_render::MouseEvent evt(aMouseEvent->x(), aMouseEvent->y(), aMouseEvent->modifiers(),
                                 aMouseEvent->button(), aMouseEvent->buttons());
    if (getNavigationCam()->processMouseMoveEvent(&evt)) {
        sendCamUpdate(-1,false);
    }
}

void
ImageView::keyPressEvent(QKeyEvent * aKeyEvent)
{
    auto getDenoiseCondition = [&]() {
        return (getFbReceiver()->getBeautyDenoiseMode() != mcrt_dataio::ClientReceiverFb::DenoiseMode::DISABLE);
    };

    mTelemetryOverlay = getFbReceiver()->getTelemetryOverlayActive();
    mDenoise = getDenoiseCondition();

    arras_render::KeyEvent evt(arras_render::KeyAction_Press,
                               aKeyEvent->key(),
                               aKeyEvent->modifiers(),
                               aKeyEvent->isAutoRepeat());
    // std::cerr << ">> ImageView.cc ===>>> PRESS-Key <<<=== " << evt.show() << '\n'; // for debug

    if (evt.getPress() == arras_render::KeyAction_Press && !evt.getAutoRepeat()) {
        if (evt.getKey() == arras_render::Key_SHIFT) mPressShiftKey = true;
        if (evt.getKey() == arras_render::Key_ALT) mPressAltKey = true;
        if (evt.getKey() == arras_render::Key_CTRL) mPressCtrlKey = true;
    }

    arras_render::NavigationCam* cam = getNavigationCam();
    bool activeKey = false;
    if (cam->processKeyboardEvent(&evt)) {
        sendCamUpdate(1.0f);
    } else if (processKeyboardEvent(&evt)) {
        std::cerr << ">> ImageView.cc processed ImageView::keyPressEvent() processKeyboardEvent()\n";
    } else if (telemetryPanelKeyboardEvent(&evt, activeKey)) {
        std::cerr << ">> ImageView.cc processed ImageView::keyPressEvent() telemetryPanelKeyboardEvent()\n";
    } else {
        if (!activeKey) {
            std::cerr << ">> ImageView.cc no KeyboardEvent " << evt.show() << "\n";
        }
    }
}

void
ImageView::keyReleaseEvent(QKeyEvent * aKeyEvent)
{
    arras_render::KeyEvent evt(arras_render::KeyAction_Release,
                               aKeyEvent->key(),
                               aKeyEvent->modifiers(),
                               aKeyEvent->isAutoRepeat());
    // std::cerr << ">> ImageView.cc +++>>> release-Key <<<+++ " << evt.show() << '\n'; // for debug

    if (evt.getPress() == arras_render::KeyAction_Release && !evt.getAutoRepeat()) {
        if (evt.getKey() == arras_render::Key_SHIFT) mPressShiftKey = false;
        if (evt.getKey() == arras_render::Key_ALT) mPressAltKey = false;
        if (evt.getKey() == arras_render::Key_CTRL) mPressCtrlKey = false;
    }

    bool activeKey = false;
    if (getNavigationCam()->processKeyboardEvent(&evt)) {
        sendCamUpdate(1.0f); 
    } else if (telemetryPanelKeyboardEvent(&evt, activeKey)) {
        std::cerr << ">> ImageView.cc processed ImageView::keyReleaseEvent() telemetryPanelKeyboardEvent()\n";
    }
}

std::string
ImageView::pathVisClientInfoCallBack()
{
    auto modifierKeyStatus = [&]() {
        auto showKey = [](const bool st, const std::string& name) {
            using C3 = arras_render::telemetry::C3;
            const C3 bgC(0,255,255);
            const C3 fgC = bgC.bestContrastCol();
            std::ostringstream ostr;
            if (st) ostr << fgC.setFg() << bgC.setBg() << name << C3::resetFgBg();
            else ostr << name;
            return ostr.str();
        };
        std::ostringstream ostr;
        ostr << showKey(mPressShiftKey, "Shift") << ' '
             << showKey(mPressAltKey, "Alt") << ' '
             << showKey(mPressCtrlKey, "Ctrl") << ' ';
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "==>> Camera <<==\n";
    if (mActiveCameraType == arras_render::CameraType::FREE_CAM) {
        ostr << mFreeCam.telemetryPanelInfo();
    } else { // OrbitCam
        ostr << mOrbitCam.telemetryPanelInfo();
    }
    ostr << "\n\n"
         << "posMoveStep:" << mPosMoveStep << '\n'
         << mPathVisCamCheckpoint.telemetryPanelInfo() << '\n'
         << '\n'
         << modifierKeyStatus();

    return ostr.str();
}

void
ImageView::parserConfigure()
{
    mParser.description("ImageView commands");

    mParser.opt("pathVisCamAnim", "<n|show>", "set path visualizer camera animation segment total",
                [&](Arg& arg) {
                    if (arg() == "show") arg++;
                    else mPathVisCamAnimSegmentTotal = (arg++).as<unsigned>(0);
                    return arg.msg(std::to_string(mPathVisCamAnimSegmentTotal) + '\n');
                });
    mParser.opt("showCamXform", "", "show navigate camera xform",
                [&](Arg& arg) { return arg.msg(showNavigateCamXform() + '\n'); });
    mParser.opt("currCamNear", "<near>", "update current camera's near value",
                [&](Arg& arg) {
                    const float near = (arg++).as<float>(0);
                    cmdUpdateCurrCamNear(near);
                    return true;
                });
}

std::string
ImageView::showNavigateCamXform()
{
    auto showMtx = [](const scene_rdl2::math::Mat4f& mtx) {
        auto showV = [](const float f) {
            std::ostringstream ostr;
            ostr << std::setw(10) << std::fixed << std::setprecision(5) << f;
            return ostr.str();
        };
        std::ostringstream ostr;
        ostr << showV(mtx.vx.x) << ", " << showV(mtx.vx.y) << ", " << showV(mtx.vx.z) << ", " << showV(mtx.vx.w) << '\n'
             << showV(mtx.vy.x) << ", " << showV(mtx.vy.y) << ", " << showV(mtx.vy.z) << ", " << showV(mtx.vy.w) << '\n'
             << showV(mtx.vz.x) << ", " << showV(mtx.vz.y) << ", " << showV(mtx.vz.z) << ", " << showV(mtx.vz.w) << '\n'
             << showV(mtx.vw.x) << ", " << showV(mtx.vw.y) << ", " << showV(mtx.vw.z) << ", " << showV(mtx.vw.w);
        return ostr.str();
    };

    const scene_rdl2::math::Mat4f camXform = getNavigationCam()->update(0.0f);

    std::ostringstream ostr;
    ostr << "navigateCamXform {\n"
         << scene_rdl2::str_util::addIndent(showMtx(camXform)) + '\n'
         << "}";
    return ostr.str();
}

void
ImageView::cmdUpdateCurrCamNear(const float near)
{
    mRdlCam->beginUpdate();
    mRdlCam->set(scene_rdl2::rdl2::Camera::sNearKey, near);
    mRdlCam->endUpdate();
    sendSceneUpdate(true);
}
