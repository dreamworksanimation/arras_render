// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NavigationCam.h"

///
/// Controls:
///
/// alt + LMB                   - orbit around pivot point
/// alt + MMB                   - pan
/// alt + RMB                   - dolly (zoom in and out)
/// alt + LMB + RMB             - roll
/// ctrl + LMB                  - refocus on point under mouse cursor
/// 
/// W                           - forward
/// S                           - backward
/// A                           - left
/// D                           - right
/// Space                       - up
/// C                           - down
/// Q                           - slow down
/// E                           - speed up
/// R                           - reset to original startup location in world
/// U                           - upright camera (remove roll)
/// T                           - print current camera matrix to console in lua format
/// F                           - alternate key to refocus on point under mouse cursor
///

namespace arras_render {
    
struct Camera;

class OrbitCam : public NavigationCam
{
public:
    using CalcFocusPointCallBack = std::function<scene_rdl2::math::Vec3f()>;

    OrbitCam();
    ~OrbitCam();

    void setCalcFocusPointCallBack(const CalcFocusPointCallBack& callBack) { mCalcFocusPointCallBack = callBack; }
    void setNear(const float near);

    /// The active render context should be set before calling this function.
    scene_rdl2::math::Mat4f resetTransform(const scene_rdl2::math::Mat4f& xform,
                                           const bool makeDefault) override;

    scene_rdl2::math::Mat4f update(const float dt) override;

    /// Returns true if the input was used, false if discarded.
    bool processKeyboardEvent(const KeyEvent* event) override;
    bool processMousePressEvent(const MouseEvent* event) override;
    bool processMouseMoveEvent(const MouseEvent* event) override;
    void clearMovementState() override;

    scene_rdl2::math::Mat4f getInitialTransform();

    void setCOI(const scene_rdl2::math::Vec3f& coi);
    scene_rdl2::math::Vec3f getCOI() const;

    std::string telemetryPanelInfo() const;

private:
    enum class MouseMode : int {
        NONE,
        ORBIT,
        PAN,
        DOLLY,
        ROLL,
        ROTATE_CAMERA,
    };

    // Run a center-pixel "pick" operation to compute camera focus
    void pickFocusPoint();

    void recenterCamera();
    bool pick(scene_rdl2::math::Vec3f& hitPoint) const;
    scene_rdl2::math::Mat4f makeMatrix(const Camera& camera) const;
    void printCameraMatrices() const;

    //------------------------------

    CalcFocusPointCallBack mCalcFocusPointCallBack;

    Camera* mCamera;

    float mSpeed {1.0f};
    uint32_t mInputState;
    MouseMode mMouseMode;
    int mMouseX;
    int mMouseY;

    bool mInitialTransformSet;
    bool mInitialFocusSet;
    scene_rdl2::math::Vec3f mInitialPosition;
    scene_rdl2::math::Vec3f mInitialViewDir;
    scene_rdl2::math::Vec3f mInitialUp;
    float mInitialFocusDistance;
};

} // namespace arras_render
