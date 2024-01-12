// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ImageView.h"

#include <algorithm> // std::find
#include <cmath>
#include <fstream>
#include <iostream>
#include <list>
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
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

#include <mcrt_messages/RDLMessage.h>
#include <mcrt_messages/RenderMessages.h>
#include <mcrt_messages/JSONMessage.h>
#include <mcrt_messages/CreditUpdate.h>
#include <mcrt_messages/GenericMessage.h>

#include "encodingUtil.h"
#include "outputRate.h"

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

using namespace arras_render;

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

    unsigned int width = mImgWidth / mImgScale;
    unsigned int height = mImgHeight / mImgScale;
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
        // We don¡Çt need to update the image reso here.Because these values have been already set up
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

void
ImageView::populateRGBFrame()
{
    if (mBlankDisplay) {
#ifdef DEBUG_MSG_POPULATE_RGB_FRAME
        std::cerr << ">> ImageView.cc populateRGBFrame() before memset()\n";
#endif DEBUG_MSG_POPULATE_RGB_FRAME
        std::memset(&mRgbFrame[0], 0, mRgbFrame.size());
#ifdef DEBUG_MSG_POPULATE_RGB_FRAME
        std::cerr << ">> ImageView.cc populateRGBFrame() after memset()\n";
#endif DEBUG_MSG_POPULATE_RGB_FRAME
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
        int offPix = v * mImgWidth + u;
        int offset = offPix * 3;
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
    int width = image.width();
    int height = image.height();

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

    auto now = std::chrono::steady_clock::now();
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

void
ImageView::sendCommand(const std::string &cmd)
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
    } else if (cmd == "sendStart") {
        handleStartStop(true);
    } else if (cmd == "sendStop") {
        handleStartStop(false);
    }
}

void
ImageView::sendCommand(std::function<const arras4::api::MessageContentConstPtr()> callBack)
//
// for debug console
//
{
    mSdk->sendMessage(callBack());
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
    unsigned w = mImgWidth / mImgScale;
    unsigned h = mImgHeight / mImgScale;
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
    
    std::vector<int> subViewport = {xMin, yMin, xMax, yMax};
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
    int baseX = this->x();
    int baseY = this->y();

    const QRect scrollGeom = mScrollArea->frameGeometry();
    int scrollBaseX = scrollGeom.left();
    int scrollBaseY = scrollGeom.top();

    const QRect scrollRect = mScrollArea->childrenRect();
    int t = scrollRect.top();
    
    topLeftX = baseX + scrollBaseX + 1;
    topLeftY = baseY + scrollBaseY + t + 37;
}

//------------------------------------------------------------------------------------------

void
ImageView::handleStartStop(bool start)
{
    std::lock_guard<std::mutex> guard(mSceneMux);
    mPaused = !start;

    std::string msgDesc = start ? "Start" : "Stop";

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
    mFreeCamera.resetTransform(camXform,true);

    mCamPlayback.setSendCamCallBack([&](const scene_rdl2::math::Mat4f& camMtx) {
            mRdlCam->beginUpdate();
            mRdlCam->set(scene_rdl2::rdl2::Node::sNodeXformKey, scene_rdl2::math::toDouble(camMtx));
            mRdlCam->endUpdate();
            sendSceneUpdate(true);
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

        setOutputRate(*mSdk, mAovInterval, 1, priorityAov, 1);
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

    unsigned int width = mImgWidth / mImgScale;
    unsigned int height = mImgHeight / mImgScale;
    mImage->setFixedSize(width, height);
    mScrollArea->setMaximumSize(width+SCROLL_PAD, height+SCROLL_PAD);
    QSize buttonSize = mButtonRow->sizeHint();
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

    scene_rdl2::math::Color newRdlColor(red, green, blue);

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

        auto rdlColor = mCurLight->get(scene_rdl2::rdl2::Light::sColorKey);
        std::cout << "Current color " << rdlColor << std::endl;

        QColor curColor;
        curColor.setRgbF(rdlColor.r, rdlColor.g, rdlColor.b);

        QColor newColor = QColorDialog::getColor(curColor,
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

    camMat = mFreeCamera.update(dt);
    if (mCamPlayback.getMode() == arras_render::CamPlayback::Mode::MODE_REC) {
        mCamPlayback.recCam(camMat);
    } else {
        mCamPlayback.saveCam(camMat); // save camera matrix only
    }

    mRdlCam->beginUpdate();
    mRdlCam->set(scene_rdl2::rdl2::Node::sNodeXformKey, scene_rdl2::math::toDouble(camMat));
    mRdlCam->endUpdate();
    sendSceneUpdate(forceUpdate);
}

void
ImageView::sendSceneUpdate(bool forceUpdate)
{
    // make sure we don't update too often
    if (!forceUpdate &&
        (mMinUpdateInterval > std::chrono::steady_clock::duration::zero())) {
        auto now = std::chrono::steady_clock::now();
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
    MouseEvent evt(aMouseEvent->x(), aMouseEvent->y(), aMouseEvent->modifiers(),
                   aMouseEvent->button(), aMouseEvent->buttons());
    mFreeCamera.processMousePressEvent(&evt);
    sendCamUpdate(-1);
}

void
ImageView::mouseReleaseEvent(QMouseEvent *aMouseEvent)
{
    MouseEvent evt(aMouseEvent->x(), aMouseEvent->y(), aMouseEvent->modifiers(),
                   aMouseEvent->button(), aMouseEvent->buttons());
    mFreeCamera.processMouseReleaseEvent(&evt);
    sendCamUpdate(-1);
}

void
ImageView::mouseMoveEvent(QMouseEvent *aMouseEvent)
{
    MouseEvent evt(aMouseEvent->x(), aMouseEvent->y(), aMouseEvent->modifiers(),
                   aMouseEvent->button(), aMouseEvent->buttons());
    mFreeCamera.processMouseMoveEvent(&evt);
    sendCamUpdate(-1,false);
}

void
ImageView::keyPressEvent(QKeyEvent * aKeyEvent)
{
    auto getDenoiseCondition = [&]() {
        return (getFbReceiver()->getBeautyDenoiseMode() != mcrt_dataio::ClientReceiverFb::DenoiseMode::DISABLE);
    };
    auto setDenoiseCondition = [&](bool flag) {
        if (flag) {
            getFbReceiver()->setBeautyDenoiseMode(mcrt_dataio::ClientReceiverFb::DenoiseMode::ENABLE);
        } else {
            getFbReceiver()->setBeautyDenoiseMode(mcrt_dataio::ClientReceiverFb::DenoiseMode::DISABLE);
        }
    };

    mFreeCamera.setTelemetryOverlay(getFbReceiver()->getTelemetryOverlayActive());
    mFreeCamera.setDenoise(getDenoiseCondition());
    mFreeCamera.initSwitchTelemetryPanel();

    KeyEvent evt(1,aKeyEvent->key(),aKeyEvent->modifiers());
    if (mFreeCamera.processKeyboardEvent(&evt, true)) {
        sendCamUpdate(1.0f);
    } else {
        if (mFreeCamera.getTelemetryOverlay()) {
            mFbReceiver->setTelemetryOverlayActive(true);
        } else {
            mFbReceiver->setTelemetryOverlayActive(false);
        }

        if (mFbReceiver->getTelemetryOverlayActive()) {
            if (mFreeCamera.getSwitchTelemetryPanelToParent()) {
                mFbReceiver->switchTelemetryPanelToParent();                
            } else if (mFreeCamera.getSwitchTelemetryPanelToNext()) {
                mFbReceiver->switchTelemetryPanelToNext();
            } else if (mFreeCamera.getSwitchTelemetryPanelToPrev()) {
                mFbReceiver->switchTelemetryPanelToPrev();
            } else if (mFreeCamera.getSwitchTelemetryPanelToChild()) {
                mFbReceiver->switchTelemetryPanelToChild();
            }
        }

        if (mFreeCamera.getDenoise()) {
            setDenoiseCondition(true);
        } else {
            setDenoiseCondition(false);
        }
    }
}

void
ImageView::keyReleaseEvent(QKeyEvent * aKeyEvent)
{
    KeyEvent evt(2,aKeyEvent->key(),aKeyEvent->modifiers());
    if (mFreeCamera.processKeyboardEvent(&evt, false)) {
        sendCamUpdate(1.0f);
    }
}
