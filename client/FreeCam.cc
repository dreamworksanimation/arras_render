// Copyright 2023-2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "FreeCam.h"
#include "TelemetryPanelUtil.h"

// must be between 0 and 1
#define FREECAM_MAX_DAMPENING   0.1f

using namespace scene_rdl2;
using namespace scene_rdl2::math;

namespace {

Mat4f
makeMatrix(const float yaw, const float pitch, const float roll, const Vec3f& pos)
{
    Mat4f rotYaw, rotPitch, rotRoll;
    rotYaw.setToRotation(Vec4f(0.0f, 1.0f, 0.0f, 0.0f), yaw);
    rotPitch.setToRotation(Vec4f(1.0f, 0.0f, 0.0f, 0.0f), pitch);
    rotRoll.setToRotation(Vec4f(0.0f, 0.0f, 1.0f, 0.0f), roll);
    const Mat4f rotation = rotRoll * rotPitch * rotYaw;

    return rotation * Mat4f::translate(Vec4f(pos.x, pos.y, pos.z, 1.0f));
}

// Print out matrix in lua format so it can be pasted into an rdla file.
void
printMatrix(const char* comment, const Mat4f& m)
{
    std::cout << "-- " << comment << "\n"
              << "[\"node xform\"] = Mat4("
              << m.vx.x << ", " << m.vx.y << ", " << m.vx.z << ", " << m.vx.w << ", "
              << m.vy.x << ", " << m.vy.y << ", " << m.vy.z << ", " << m.vy.w << ", "
              << m.vz.x << ", " << m.vz.y << ", " << m.vz.z << ", " << m.vz.w << ", "
              << m.vw.x << ", " << m.vw.y << ", " << m.vw.z << ", " << m.vw.w << "),\n"
              << std::endl;
}

}   // end of anon namespace

//------------------------------------------------------------------------------------------

namespace arras_render {

enum class FreeCamFlag : int {
    FREECAM_FORWARD     = 0x0001,
    FREECAM_BACKWARD    = 0x0002,
    FREECAM_LEFT        = 0x0004,
    FREECAM_RIGHT       = 0x0008,
    FREECAM_UP          = 0x0010,
    FREECAM_DOWN        = 0x0020,
    FREECAM_SLOW_DOWN   = 0x0040,
    FREECAM_SPEED_UP    = 0x0080
};

FreeCam::FreeCam()
    : mPosition(0.0f, 0.0f, 0.0f)
    , mVelocity(0.0f, 0.0f, 0.0f)
    , mYaw(0.0f)
    , mPitch(0.0f)
    , mRoll(0.0f)
    , mSpeed(1.0f)
    , mDampening(1.0f)
    , mMouseSensitivity(0.004f)
    , mInputState(0)
    , mMouseMode(MouseMode::NONE)
    , mMouseX(0)
    , mMouseY(0)
    , mMouseDeltaX(0)
    , mMouseDeltaY(0)
    , mInitialTransformSet(false)
{
}

FreeCam::~FreeCam()
{
}

Mat4f
FreeCam::resetTransform(const Mat4f& xform, const bool makeDefault)
{
    if (!mInitialTransformSet || makeDefault) {
        mInitialTransform = xform;
        mInitialTransformSet = true;
    }

    mPosition = asVec3(xform.row3());
    mVelocity = Vec3f(zero);

    const Vec3f viewDir = -normalize(asVec3(xform.row2()));

    mYaw = 0.0f;
    if (viewDir.x * viewDir.x + viewDir.z * viewDir.z > 0.00001f) {
        mYaw = scene_rdl2::math::atan2(-viewDir.x, -viewDir.z);
    }

    // We aren't extracting the entire range of possible pitches here, just the
    // ones which the freecam can natively handle. Because of this, not all camera
    // orientations are supported.
    mPitch = scene_rdl2::math::asin(viewDir.y);

    // Compute a matrix which only contains the roll so we can extract it out.
    const Mat4f noRoll = makeMatrix(mYaw, mPitch, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    const Mat4f rollOnly = xform * noRoll.transposed();
    const Vec3f xAxis = normalize(asVec3(rollOnly.row0()));
    mRoll = scene_rdl2::math::atan2(xAxis.y, xAxis.x);

    mInputState = 0;
    mMouseMode = MouseMode::NONE; 
    mMouseX = mMouseY = 0;    
    mMouseDeltaX = mMouseDeltaY = 0;

    return makeMatrix(mYaw, mPitch, mRoll, mPosition);
}

Mat4f
FreeCam::update(const float dt)
{
    // Compute some amount to change our current velocity.
    Vec3f deltaVelocity = Vec3f(zero);
    const float movement = mSpeed * 0.5f;

    // Process keyboard input.
    if (mInputState & static_cast<unsigned>(FreeCamFlag::FREECAM_FORWARD)) {
        deltaVelocity += Vec3f(0.0f, 0.0f, -movement);
    }
    if (mInputState & static_cast<unsigned>(FreeCamFlag::FREECAM_BACKWARD)) {
        deltaVelocity += Vec3f(0.0f, 0.0f, movement);
    }
    if (mInputState & static_cast<unsigned>(FreeCamFlag::FREECAM_LEFT)) {
        deltaVelocity += Vec3f(-movement, 0.0f, 0.0f);
    }
    if (mInputState & static_cast<unsigned>(FreeCamFlag::FREECAM_RIGHT)) {
        deltaVelocity += Vec3f(movement, 0.0f, 0.0f);
    }
    if (mInputState & static_cast<unsigned>(FreeCamFlag::FREECAM_UP)) {
        deltaVelocity += Vec3f(0.0f, movement, 0.0f);
    }
    if (mInputState & static_cast<unsigned>(FreeCamFlag::FREECAM_DOWN)) {
        deltaVelocity += Vec3f(0.0f, -movement, 0.0f);
    }

    if (mInputState & static_cast<unsigned>(FreeCamFlag::FREECAM_SLOW_DOWN)) {
        static constexpr float minSpeed = 0.01f;
        mSpeed *= 0.5f;
        if (mSpeed <= minSpeed) mSpeed = minSpeed;
        std::cerr << ">> FreeCam.cc SlowDown mSpeed:" << mSpeed << '\n';
    }
    if (mInputState & static_cast<unsigned>(FreeCamFlag::FREECAM_SPEED_UP)) {
        static constexpr float maxSpeed = 8192.0f;
        mSpeed *= 2.0f;
        if (mSpeed > maxSpeed) mSpeed = maxSpeed;
        std::cerr << ">> FreeCam.cc SpeedUp mSpeed:" << mSpeed << '\n';
    }

    // Update the camera angles by the rotation amounts (ignore dt for this
    // since it should be instant).
    if (mMouseMode == MouseMode::MOVE) {

        // rotate mouse movement by roll before updating yaw and pitch
        float c, s;
        sincos(-mRoll, &s, &c);

        const float dx = float(mMouseDeltaX) * c - float(mMouseDeltaY) * s;
        const float dy = float(mMouseDeltaY) * c + float(mMouseDeltaX) * s;

        mYaw -= dx * mMouseSensitivity;
        mPitch -= dy * mMouseSensitivity;

    } else if (mMouseMode == MouseMode::ROLL) {
        mRoll += float(mMouseDeltaX) * mMouseSensitivity;
    }
    mMouseDeltaX = mMouseDeltaY = 0;

    // Clip camera pitch to prevent Gimbal Lock.
    const float halfPi = sHalfPi;
    mPitch = clamp(mPitch, -halfPi, halfPi);

    // Transform deltaVelocity into current camera coordinate system.
    const Mat4f rotation = makeMatrix(mYaw, mPitch, mRoll, zero);
    deltaVelocity = transform3x3(rotation, deltaVelocity);

    mVelocity += deltaVelocity;

    // Scale back velocity to mSpeed if too big.
    const float len = mVelocity.length();
    if (len > mSpeed) {
        mVelocity *= (mSpeed / len);
    }

    // Integrate position.
    mPosition += mVelocity * dt;

    // Apply dampening to velocity.
    mVelocity *= min(mDampening * dt, FREECAM_MAX_DAMPENING);

    return makeMatrix(mYaw, mPitch, mRoll, mPosition);
}

bool
FreeCam::processKeyboardEvent(const KeyEvent* event)
{
    bool used = false;

    if (event->getModifiers() == QT_NoModifier) {

        used = true;

        if (event->getPress() == KeyAction_Press) {
            // Check for pressed keys.
            switch (event->getKey()) {
            case Key_W:     mInputState |= static_cast<unsigned>(FreeCamFlag::FREECAM_FORWARD);     break;
            case Key_S:     mInputState |= static_cast<unsigned>(FreeCamFlag::FREECAM_BACKWARD);    break;
            case Key_A:     mInputState |= static_cast<unsigned>(FreeCamFlag::FREECAM_LEFT);        break;
            case Key_D:     mInputState |= static_cast<unsigned>(FreeCamFlag::FREECAM_RIGHT);       break;
            case Key_Space: mInputState |= static_cast<unsigned>(FreeCamFlag::FREECAM_UP);          break;
            case Key_C:     mInputState |= static_cast<unsigned>(FreeCamFlag::FREECAM_DOWN);        break;
            case Key_Q:     mInputState |= static_cast<unsigned>(FreeCamFlag::FREECAM_SLOW_DOWN);   break;
            case Key_E:     mInputState |= static_cast<unsigned>(FreeCamFlag::FREECAM_SPEED_UP);    break;
            case Key_T:     printCameraMatrices();              break;
            case Key_U:     mRoll = 0.0f;                       break;
            case Key_R:
                if(mInitialTransformSet) {
                    clearMovementState();
                    resetTransform(mInitialTransform, false);
                }
                break;

            default:
                used = false;
                // std::cerr << ">> FreeCam.cc key:0x" << std::hex << event->key() << '\n'; // useful debug message
                break;
            }
        } else {
            // Check for released keys.
            switch (event->getKey()) {
            case Key_W:     mInputState &= ~static_cast<unsigned>(FreeCamFlag::FREECAM_FORWARD);    break;
            case Key_S:     mInputState &= ~static_cast<unsigned>(FreeCamFlag::FREECAM_BACKWARD);   break;
            case Key_A:     mInputState &= ~static_cast<unsigned>(FreeCamFlag::FREECAM_LEFT);       break;
            case Key_D:     mInputState &= ~static_cast<unsigned>(FreeCamFlag::FREECAM_RIGHT);      break;
            case Key_Space: mInputState &= ~static_cast<unsigned>(FreeCamFlag::FREECAM_UP);         break;
            case Key_C:     mInputState &= ~static_cast<unsigned>(FreeCamFlag::FREECAM_DOWN);       break;
            case Key_Q:     mInputState &= ~static_cast<unsigned>(FreeCamFlag::FREECAM_SLOW_DOWN);  break;
            case Key_E:     mInputState &= ~static_cast<unsigned>(FreeCamFlag::FREECAM_SPEED_UP);   break;
            default: used = false;
            }
        }
    }

    return used;
}

bool
FreeCam::processMousePressEvent(const MouseEvent* event)
{
    mMouseMode = MouseMode::NONE;
    if (event->getButtons() == QT_LeftButton &&
        event->getModifiers() == QT_NoModifier) {
        mMouseMode = MouseMode::MOVE;
    } else if (event->getButtons() == (QT_LeftButton | QT_RightButton) &&
               event->getModifiers() == QT_AltModifier) {
        mMouseMode = MouseMode::ROLL;
    }

    if (mMouseMode != MouseMode::NONE) {
        mMouseX = event->getX();
        mMouseY = event->getY();
        mMouseDeltaX = mMouseDeltaY = 0;
        return true;
    }
    return false;
}

bool
FreeCam::processMouseReleaseEvent(const MouseEvent* event)
{
    if (event->getButton() == QT_LeftButton) {
        mMouseMode = MouseMode::NONE;
        return true;
    }
    return false;
}

bool
FreeCam::processMouseMoveEvent(const MouseEvent* event)
{
    if (mMouseMode == MouseMode::MOVE || mMouseMode == MouseMode::ROLL) {
        mMouseDeltaX += (event->getX() - mMouseX); 
        mMouseDeltaY += (event->getY() - mMouseY); 
        mMouseX = event->getX();
        mMouseY = event->getY();
        return true;
    }
    return false;
}

void
FreeCam::clearMovementState()
{
    mVelocity = Vec3f(0.0f, 0.0f, 0.0f);
    mInputState = 0;
    mMouseMode = MouseMode::NONE;
    mMouseX = 0;
    mMouseY = 0;
}

std::string
FreeCam::telemetryPanelInfo() const
{
    using namespace arras_render::telemetry;
    const C3 c3bg(255, 0, 0);
    const C3 c3fg = c3bg.bestContrastCol();

    std::ostringstream ostr;
    ostr << c3fg.setFg() << c3bg.setBg() << "----- Free -----" << C3::resetFgBg() << '\n'
         << outF("Pos X:", mPosition[0]) << '\n'
         << outF("    Y:", mPosition[1]) << '\n'
         << outF("    Z:", mPosition[2]) << '\n'
         << outF("  Yaw:", mYaw) << '\n'
         << outF("Pitch:", mPitch) << '\n'
         << outF(" Roll:", mRoll) << '\n'
         << outF("Speed:", mSpeed); 
    return ostr.str();
}

void
FreeCam::printCameraMatrices() const
{
    const Mat4f fullMat = makeMatrix(mYaw, mPitch, mRoll, mPosition);
    const Mat4f zeroPitchMat = makeMatrix(mYaw, 0.0f, 0.0f, mPosition);

    printMatrix("Full matrix containing rotation and position.", fullMat);
    printMatrix("Matrix containing world xz rotation and position.", zeroPitchMat);
}

} // namespace arras_render
