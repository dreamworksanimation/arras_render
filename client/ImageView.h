// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#ifndef IMAGE_VIEW_H_
#define IMAGE_VIEW_H_

#ifndef Q_MOC_RUN
#include <scene_rdl2/scene/rdl2/BinaryWriter.h>
#include <scene_rdl2/scene/rdl2/Camera.h>
#include <scene_rdl2/scene/rdl2/Light.h>
#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <mcrt_dataio/client/receiver/ClientReceiverFb.h>
#endif

#include <sdk/sdk.h>
#include "NotifiedValue.h"
#include "Scripting.h"
#include "CamPlayback.h"
#include "FreeCam.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include <QObject>
#include <QComboBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QScrollArea>
#include <QLabel>
#include <QPen>
#include <QPushButton>
#include <QVBoxLayout>

namespace ImageViewDefaults {
const float DEFAULT_ZOOM_AMT = 20.0f;
const std::string DEFAULT_FONT_NAME("Arial");
const int DEFAULT_FONT_SIZE = 32;
}

class ImageView : public QWidget
{
    Q_OBJECT
public:
    ImageView(std::shared_ptr<mcrt_dataio::ClientReceiverFb> pFbReceiver,
              std::unique_ptr<scene_rdl2::rdl2::SceneContext> sceneCtx,
              bool overlay=false,
              const std::string& overlayFontName=ImageViewDefaults::DEFAULT_FONT_NAME,
              int overlayFontSize=ImageViewDefaults::DEFAULT_FONT_SIZE,
              const std::string& sessionName = std::string(),
              unsigned short numMcrtComps=1,
              unsigned short numMcrtCompsMax=1,
              unsigned aovInterval=1,
              const std::string& scriptName = std::string(),
              bool exitAfterScript = false,
              std::chrono::steady_clock::duration minUpdateInterval =  std::chrono::steady_clock::duration::zero(),
              bool noInitialScale = false,
              const std::chrono::time_point<std::chrono::steady_clock>& renderStart = \
                  std::chrono::steady_clock::now(),
              QWidget* parent = 0);
    virtual ~ImageView();
    
    void setup(std::shared_ptr<arras4::sdk::SDK>& sdk);

    std::mutex& getFrameMux() { return mFrameMux; }

    void setInitialCondition();

    void displayFrame();
    void clearDisplayFrame();
    void exitProgram();

    void setStatusOverlay(short index, std::string message);

    NotifiedValue<float> mRenderProgress;
    NotifiedValue<int> mRenderInstance; // unique for each render

    //------------------------------

    void sendCommand(const std::string &cmd); // for debug console
    void sendCommand(std::function<const arras4::api::MessageContentConstPtr()> callBack); // for debug console
    void changeImageSize(int width, int height); // for debug console
    void changeROI(int xMin, int yMin, int xMax, int yMax); // for debug console
    void changeROIoff(); // for debug console

    void setOverlayParam(unsigned offsetX, unsigned offsetY, unsigned fontSize);

    const scene_rdl2::rdl2::SceneContext *getSceneContext() const { return mSceneCtx.get(); }
    scene_rdl2::rdl2::SceneContext& getSceneContext2() { return *mSceneCtx; }
    std::shared_ptr<mcrt_dataio::ClientReceiverFb> getFbReceiver() const { return mFbReceiver; }
    arras_render::CamPlayback& getCamPlayback() { return mCamPlayback; }

    void getImageDisplayWidgetPos(int& topLeftX, int& topLeftY);

public Q_SLOTS:
    void displayFrameSlot();
    void handleStart();
    void handleStop();
    void handlePause();
    void handleExitProgram();

    void handlePrevOutput();
    void handleNextOutput();
    void handleRunScript();
    void handleAovSelect(int index);

    void handleLightSelect(int index);
    void handleScaleSelect(int index);
    void handleColorButton();
    void handleNewColor(float red, float green, float blue);

    void handleSendCredit(int);
    void handleStatusOverlay(short, QString);

Q_SIGNALS:
    void displayFrameSignal();
    void setNewColorSignal(float red, float green, float blue);
    void statusOverlaySignal(short, QString);
    void sendCredit(int);
    void exitProgramSignal();

protected:
    void mouseMoveEvent(QMouseEvent* aMouseEvent);
    void mousePressEvent(QMouseEvent* aMouseEvent);
    void mouseReleaseEvent(QMouseEvent* aMouseEvent);
    void keyPressEvent(QKeyEvent * event);
    void keyReleaseEvent(QKeyEvent * event);

private:
    void initImage();
    void initCam();
    void initLights();
    void printCamInfo() const;
    void changeRenderOutput(bool updateAovCombo=true);
    void addOverlay(QImage& image);
    void handleStartStop(bool start);
    void sendCamUpdate(float dt=-1.f, bool forceUpdate = true); 
    void sendSceneUpdate(bool forceUpdate = true);
    void updateOutputsComboBox();

    void populateRGBFrame();
    bool savePPM(const std::string& filename) const; // for debug
    bool saveQImagePPM(const std::string& filename, const QImage& image) const; // for debug

    bool mOverlay;
    const std::string mSessionName;
    const unsigned short mNumMcrtComps, mNumMcrtCompsMax;

    // QT stuff
    int mFontSize;
    std::unique_ptr<QLabel> mImage;
    std::unique_ptr<QScrollArea> mScrollArea;
    std::unique_ptr<QVBoxLayout> mMainLayout;
    std::unique_ptr<QGroupBox> mButtonRow;
    std::unique_ptr<QHBoxLayout> mButtonLayout;
    std::unique_ptr<QPushButton> mButStart;
    std::unique_ptr<QPushButton> mButStop;
    std::unique_ptr<QPushButton> mButPause;
    std::unique_ptr<QPushButton> mButPrevOutput;
    std::unique_ptr<QPushButton> mButNextOutput;
    std::unique_ptr<QPushButton> mButRunScript;
    std::unique_ptr<QComboBox> mCboOutputs;
    std::unique_ptr<QComboBox> mCboLights;
    std::unique_ptr<QComboBox> mScaleCombo;
    std::unique_ptr<QPushButton> mButColor;
    std::unique_ptr<QFont> mFont;
    std::unique_ptr<QPen> mFontColor;

    // Arras & Moonray
    std::mutex mSceneMux;
    std::shared_ptr<arras4::sdk::SDK> mSdk;

    std::shared_ptr<mcrt_dataio::ClientReceiverFb> mFbReceiver;
    std::unique_ptr<scene_rdl2::rdl2::SceneContext> mSceneCtx;
    const unsigned mAovInterval;

    // Camera
    FreeCam mFreeCamera;
    scene_rdl2::rec_time::RecTime mCameraUpdateTime;
    scene_rdl2::rdl2::Camera* mRdlCam;
    scene_rdl2::rdl2::Light* mCurLight;

    arras_render::CamPlayback mCamPlayback;

    // Rendered Frame data
    bool mBlankDisplay;
    std::mutex mFrameMux;
    std::vector<unsigned char> mRgbFrame;
    std::vector<unsigned char> mRgbFrameCopy;
    std::vector<std::string> mOutputNames;
    unsigned int mNumBuiltinPasses;
    std::string mCurrentOutput;

    std::chrono::time_point<std::chrono::steady_clock> mRenderStart;
    std::atomic<bool> mPaused;
    bool mReceivedFirstFrame;
    unsigned int mNumChannels;
    unsigned int mImgWidth;
    unsigned int mImgHeight;
    unsigned int mImgScale;

    Scripting mScripting;
    std::vector<std::string> mStatusOverlay;

    unsigned mOverlayXOffset;
    unsigned mOverlayYOffset;
    std::string mOverlayFontName;

    std::chrono::steady_clock::duration mMinUpdateInterval;

    // There is some possibility to send message by 2 different threads and we need MTsafe send operation.
    std::mutex mMutexSendMessage; // mutex for sendMessage
};

#endif /* IMAGE_VIEW_H_ */
