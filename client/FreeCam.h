// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//    we could not load QT into a computation without running out of process space for TLS.
//    either had to rewrite the little QT we needed or get libgc recompiled with more TLS and
//    that would impact the entire project so went this route...

///
/// Controls:
///
/// LMB + Mouse move    - rotate around camera position
/// alt + LMB + RMB     - roll
/// 
/// W                   - forward
/// S                   - backward
/// A                   - left
/// D                   - right
/// Space               - up
/// C                   - down
/// Q                   - slow down
/// E                   - speed up
/// R                   - reset to original startup location in world
/// U                   - upright camera (remove roll)
/// T                   - print current camera matrix to console in lua format
/// F                   - telemetry overlay enable/disable toggle
/// G, Apostrophe(')    - switch telemetry panel to next
/// Semicolon(;)        - switch telemetry panel to previous
/// SquareBracketOpen([)- switch telemetry panel to parent
/// Slash(/)            - switch telemetry panel to child
/// N                   - denoise on/off
///

#pragma once
#include "NavigationCam.h"

class FreeCam : public NavigationCam
{
public:
                        FreeCam();
                        ~FreeCam();

    /// Returns a matrix with only pitch and yaw (no roll).
    scene_rdl2::math::Mat4f  resetTransform(const scene_rdl2::math::Mat4f &xform, bool makeDefault) override;

    scene_rdl2::math::Mat4f  update(float dt) override;

    /// Returns true if the input was used, false if discarded.
    bool                processKeyboardEvent(KeyEvent* event, bool pressed) override;
    bool                processMousePressEvent(MouseEvent* event) override;
    bool                processMouseReleaseEvent(MouseEvent* event) override;
    bool                processMouseMoveEvent(MouseEvent* event) override;
    void                clearMovementState() override;

    void setTelemetryOverlay(bool sw) { mTelemetryOverlay = sw; }
    bool getTelemetryOverlay() const { return mTelemetryOverlay; }

    void initSwitchTelemetryPanel() {
        mSwitchTelemetryPanelToParent = false;
        mSwitchTelemetryPanelToNext = false;
        mSwitchTelemetryPanelToPrev = false;
        mSwitchTelemetryPanelToChild = false;
    }
    bool getSwitchTelemetryPanelToParent() const { return mSwitchTelemetryPanelToParent; }
    bool getSwitchTelemetryPanelToNext() const { return mSwitchTelemetryPanelToNext; }
    bool getSwitchTelemetryPanelToPrev() const { return mSwitchTelemetryPanelToPrev; }
    bool getSwitchTelemetryPanelToChild() const { return mSwitchTelemetryPanelToChild; }

    void setDenoise(bool sw) { mDenoise = sw; }
    bool getDenoise() const { return mDenoise; }

private:        
    enum MouseMode
    {
        NONE,
        MOVE,
        ROLL,
    };

    void                printCameraMatrices() const;

    scene_rdl2::math::Vec3f  mPosition;
    scene_rdl2::math::Vec3f  mVelocity;
    float               mYaw;
    float               mPitch;
    float               mRoll;
    float               mSpeed;
    float               mDampening;     ///< the amount by which mVelocity is dampened each update
    float               mMouseSensitivity;
    uint32_t            mInputState;
    MouseMode           mMouseMode;
    int                 mMouseX;
    int                 mMouseY;
    int                 mMouseDeltaX;
    int                 mMouseDeltaY;

    bool                mInitialTransformSet;
    scene_rdl2::math::Mat4f  mInitialTransform;

    bool mTelemetryOverlay {false};
    bool mSwitchTelemetryPanelToParent {false};
    bool mSwitchTelemetryPanelToNext {false};
    bool mSwitchTelemetryPanelToPrev {false};
    bool mSwitchTelemetryPanelToChild {false};
    bool mDenoise {false};
};
