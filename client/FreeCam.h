// Copyright 2023-2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "NavigationCam.h"

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

namespace arras_render {

class FreeCam : public NavigationCam
{
public:
    FreeCam();
    ~FreeCam();

    /// Returns a matrix with only pitch and yaw (no roll).
    scene_rdl2::math::Mat4f resetTransform(const scene_rdl2::math::Mat4f& xform,
                                           const bool makeDefault) override;

    scene_rdl2::math::Mat4f update(const float dt) override;

    /// Returns true if the input was used, false if discarded.
    bool processKeyboardEvent(const KeyEvent* event) override;
    bool processMousePressEvent(const MouseEvent* event) override;
    bool processMouseReleaseEvent(const MouseEvent* event) override;
    bool processMouseMoveEvent(const MouseEvent* event) override;
    void clearMovementState() override;

    std::string telemetryPanelInfo() const;

private:        
    enum class MouseMode : int {
        NONE,
        MOVE,
        ROLL,
    };

    void printCameraMatrices() const;

    scene_rdl2::math::Vec3f mPosition;
    scene_rdl2::math::Vec3f mVelocity;
    float mYaw;
    float mPitch;
    float mRoll;
    float mSpeed;
    float mDampening; ///< the amount by which mVelocity is dampened each update
    float mMouseSensitivity;
    uint32_t mInputState;
    MouseMode mMouseMode;
    int mMouseX;
    int mMouseY;
    int mMouseDeltaX;
    int mMouseDeltaY;

    bool mInitialTransformSet;
    scene_rdl2::math::Mat4f mInitialTransform;
};

} // namespace arras_render
