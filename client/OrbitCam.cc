// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "OrbitCam.h"
#include "TelemetryPanelUtil.h"

#include <scene_rdl2/common/math/Viewport.h>
#include <scene_rdl2/common/platform/Platform.h>
#include <scene_rdl2/render/util/StrUtil.h>

#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath> // std::isnan()
#include <iomanip>

using namespace scene_rdl2::math;

namespace {

// Print out matrix in lua format so it can be pasted into an rdla file.
void printMatrix(const char *comment, const Mat4f &m)
{
    std::cout << "-- " << comment << "\n"
    << "[\"node xform\"] = Mat4("
    << m.vx.x << ", " << m.vx.y << ", " << m.vx.z << ", " << m.vx.w << ", "
    << m.vy.x << ", " << m.vy.y << ", " << m.vy.z << ", " << m.vy.w << ", "
    << m.vz.x << ", " << m.vz.y << ", " << m.vz.z << ", " << m.vz.w << ", "
    << m.vw.x << ", " << m.vw.y << ", " << m.vw.z << ", " << m.vw.w << "),\n"
    << std::endl;
}

std::string
showMat4f(const Mat4f& m)
{
    auto showV = [](const float f) {
        std::ostringstream ostr;
        ostr << std::setw(10) << std::fixed << std::setprecision(5) << f;
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "Mat4f {\n"
         << "  " << showV(m.vx.x) << ", " << showV(m.vx.y) << ", " << showV(m.vx.z) << ", " << showV(m.vx.w) << '\n'
         << "  " << showV(m.vy.x) << ", " << showV(m.vy.y) << ", " << showV(m.vy.z) << ", " << showV(m.vy.w) << '\n'
         << "  " << showV(m.vz.x) << ", " << showV(m.vz.y) << ", " << showV(m.vz.z) << ", " << showV(m.vz.w) << '\n'
         << "  " << showV(m.vw.x) << ", " << showV(m.vw.y) << ", " << showV(m.vw.z) << ", " << showV(m.vw.w) << '\n'        
         << "}";
    return ostr.str();
}

}   // end of anon namespace

namespace arras_render {

enum class OrbitCamFlag : int {
    ORBIT_FORWARD     = 0x0001,
    ORBIT_BACKWARD    = 0x0002,
    ORBIT_LEFT        = 0x0004,
    ORBIT_RIGHT       = 0x0008,
    ORBIT_UP          = 0x0010,
    ORBIT_DOWN        = 0x0020,
    ORBIT_SLOW_DOWN   = 0x0040,
    ORBIT_SPEED_UP    = 0x0080
};

// orbit camera (taken from embree sample code)
// This camera is in world space
struct Camera {

    Camera ()
        : position(0.0f, 0.0f, -3.0f)
        , viewDir(normalize(-position))
        , up(0.0f, 1.0f, 0.0f)
        , focusDistance(1.0f)
    {}

    Xform3f camera2world () const {
        // Warning: this needs to be double precision.  If we use single then
        // there is slight imprecision introduced when computing the cross products
        // when orthonormalizing the vectors.
        // This normally wouldn't be a problem, but this camera2world matrix
        // gets fed into OrbitCam::resetTransform() when the scene is reloaded.
        // OrbitCam::resetTransform() then sets the vectors used for camera2world,
        // but those came from camera2world.  Thus camera2world is used to set
        // itself, and the old value might be identical to the new if the user
        // hasn't manipulated the camera.
        // The imprecision from the single-precision cross products causes
        // a slight difference in camera2world when there should be no change
        // at all when camera2world hasn't changed.  This causes nondeterminism
        // between successive renders in moonray_gui as this has a slight effect
        // on the ray directions each time.
        const Vec3d vz = -viewDir;
        const Vec3d vx = normalize(cross(Vec3d(up), vz));
        const Vec3d vy = normalize(cross(vz, vx));
        return Xform3f(
            static_cast<float>(vx.x), static_cast<float>(vx.y),
            static_cast<float>(vx.z), static_cast<float>(vy.x),
            static_cast<float>(vy.y), static_cast<float>(vy.z),
            static_cast<float>(vz.x), static_cast<float>(vz.y),
            static_cast<float>(vz.z), position.x, position.y, position.z);
    }

    Xform3f world2camera () const { return camera2world().inverse();}

    Vec3f world2camera(const Vec3f& p) const { return transformPoint(world2camera(),p);}
    Vec3f camera2world(const Vec3f& p) const { return transformPoint(camera2world(),p);}

    void move (float dx, float dy, float dz, const float speed)
    {
        const float moveSpeed = 0.03f;
        dx *= -moveSpeed;
        dy *= moveSpeed;
        dz *= moveSpeed;

        if (mFocusLock) {
            dx = 0.0f;
            dy = 0.0f;

            if (dz <= 0.0f) {
                const float singleStep = speed * moveSpeed;
                const float minThread = std::max(mNear * 4.0f, 0.05f);
                const float safetyDistance = singleStep * 3.0f;
                const float newDistance = focusDistance + dz;

                /* for debug
                std::cerr << ">> mSpeed:" << speed << " singleStep:" << singleStep << " dz:" << dz
                          << " focusDist:" << focusDistance
                          << " safeDist:" << safetyDistance
                          << " newDist:" << newDistance
                          << '\n';
                */

                if (newDistance < safetyDistance) {
                    if (newDistance < safetyDistance * 0.5f) {
                        if (focusDistance * 0.5f > minThread) {
                            dz = -focusDistance * 0.5f;
                        } else {
                            dz = 0.0f;
                        }
                        // std::cerr << ">>=== new dz:" << dz << " ===<<\n";
                    }
                }
            }
        }
        const Vec3f delta(dx, dy, dz);

        Vec3f coi;
        if (mFocusLock) {
            coi = position + viewDir * focusDistance; // compute current coi point
        }

        const Xform3f xfm = camera2world();
        const Vec3f ds = transformVector(xfm, delta);
        position += ds;

        if (mFocusLock) {
            focusDistance = (coi - position).length();
        }
    }

    void rotate (const float dtheta, const float dphi)
    {
        static constexpr float rotateSpeed = 0.005f;
        // in camera local space, viewDir is always (0, 0, -1)
        // and its spherical coordinate is always (PI, 0)
        const float theta = sPi - dtheta * rotateSpeed;
        const float phi   = -dphi * rotateSpeed;

        float cosPhi, sinPhi;
        sincos(phi, &sinPhi, &cosPhi);
        float cosTheta, sinTheta;
        sincos(theta, &sinTheta, &cosTheta);

        const float x = cosPhi*sinTheta;
        const float y = sinPhi;
        const float z = cosPhi*cosTheta;

        viewDir = transformVector(camera2world(), Vec3f(x, y, z));
    }

    void rotateOrbit (const float dtheta, const float dphi)
    {
        bool currentlyValid = false;
        if (scene_rdl2::math::abs(dot(up, viewDir)) < 0.999f) {
            currentlyValid = true;
        }

        static constexpr float rotateSpeed = 0.005f;
        // in camera local space, viewDir is always (0, 0, -1)
        // and its spherical coordinate is always (PI, 0)
        const float theta = sPi - dtheta * rotateSpeed;
        const float phi   = -dphi * rotateSpeed;

        float cosPhi, sinPhi;
        sincos(phi, &sinPhi, &cosPhi);
        float cosTheta, sinTheta;
        sincos(theta, &sinTheta, &cosTheta);

        const float x = cosPhi*sinTheta;
        const float y = sinPhi;
        const float z = cosPhi*cosTheta;

        const Vec3f newViewDir = transformVector(camera2world(),Vec3f(x,y,z));
        const Vec3f newPosition = position + focusDistance * (viewDir - newViewDir);

        // Don't update 'position' if dir is near parallel with the up vector
        // unless the current state of 'position' is already invalid.
        if (scene_rdl2::math::abs(dot(up, newViewDir)) < 0.999f || !currentlyValid) {
            position = newPosition;
            viewDir = newViewDir;
        }
        
        // std::cerr << ">> OrbitCam.cc rotateOrbit() " << show() << '\n'; // for debug
    }

    void dolly (const float ds)
    {
        static constexpr float dollySpeed = 0.005f;
        const float k = scene_rdl2::math::pow((1.0f-dollySpeed), ds);
        const Vec3f focusPoint = position + viewDir * focusDistance;
        position += focusDistance * (1-k) * viewDir;
        focusDistance = length(focusPoint - position);
    }

    void roll (const float ds)
    {
        static constexpr float rollSpeed = 0.005f;
        const Vec3f& axis = viewDir;
        up = transform3x3(Mat4f::rotate(Vec4f(axis[0], axis[1], axis[2], 0.0f),
            -ds * rollSpeed), up);
    }

    std::string show() const
    {
        auto showV = [](const float v) {
            std::ostringstream ostr;
            ostr << std::setw(10) << std::fixed << std::setprecision(5) << v;
            return ostr.str();
        };
        auto showVec = [&](const Vec3f& vec) {
            std::ostringstream ostr;
            ostr << "(" << showV(vec[0]) << ", " << showV(vec[1]) << ", " << showV(vec[2]) << ")";
            return ostr.str();
        };

        std::ostringstream ostr;
        ostr << "Camera {\n"
             << "        position:" << showVec(position) << '\n'
             << "         viewDir:" << showVec(viewDir) << '\n'
             << "              up:" << showVec(up) << '\n'
             << "  focuseDistance:" << focusDistance << '\n'
             << "      mFocusLock:" << scene_rdl2::str_util::boolStr(mFocusLock) << '\n'
             << "           mNear:" << mNear << '\n'
             << "}";
        return ostr.str();
    }

public:
    Vec3f position;   //!< position of camera
    Vec3f viewDir;    //!< lookat direction
    Vec3f up;         //!< up vector
    float focusDistance;

    bool mFocusLock {false};
    float mNear {0.001f};
};

//----------------------------------------------------------------------------

OrbitCam::OrbitCam()
    : mCamera(new Camera)
    , mSpeed(1.0f)
    , mInputState(0)
    , mMouseMode(MouseMode::NONE)
    , mMouseX(-1)
    , mMouseY(-1)
    , mInitialTransformSet(false)
    , mInitialFocusSet(false)
    , mInitialFocusDistance(1.0f)
{
}

OrbitCam::~OrbitCam()
{
    delete mCamera;
}

void
OrbitCam::setNear(const float near)
{
    mCamera->mNear = near;
}

Mat4f
OrbitCam::resetTransform(const Mat4f& xform, const bool makeDefault)
{
    MNRY_ASSERT(mCamera);

    mCamera->position = asVec3(xform.vw);
    mCamera->viewDir = normalize(asVec3(-xform.vz));
    mCamera->up = Vec3f(0.0f, 1.0f, 0.0f);
    mCamera->focusDistance = 1.0f;

    if (!mInitialTransformSet || makeDefault) {
        mInitialTransformSet = true;
        mInitialFocusSet = false;
        mInitialPosition = mCamera->position;
        mInitialViewDir = mCamera->viewDir;
        mInitialUp = mCamera->up;
        mInitialFocusDistance = mCamera->focusDistance;
    }

    return xform;
}

void
OrbitCam::pickFocusPoint()
{
    MNRY_ASSERT(mCamera);
    
    // Do this function only once every time we reset the default transform
    // Note: We can't do picking during resetTransform() because picking uses
    // the pbr Scene, which hasn't been initialized at that time.
    if (mInitialFocusSet) {
        return;
    }
    mInitialFocusSet = true;

    Vec3f focusPoint;
    if (pick(focusPoint)) {
        const Vec3f hitVec = focusPoint - mCamera->position;
        mCamera->viewDir = normalize(hitVec);
        mCamera->focusDistance = length(hitVec);
    }

    /* for debug
    std::cerr << ">> OrbitCam.cc pickFocusPoint()"
              << " viewDir:" << mCamera->viewDir[0] << ',' << mCamera->viewDir[1] << ',' << mCamera->viewDir[2]
              << " focusDistance:" << mCamera->focusDistance << '\n';
    */

    mInitialViewDir = mCamera->viewDir;
    mInitialFocusDistance = mCamera->focusDistance;
}

Mat4f
OrbitCam::update(const float dt)
{
    const float movement = mSpeed * dt;

    // Process keyboard input.
    if (mInputState & static_cast<unsigned>(OrbitCamFlag::ORBIT_FORWARD)) {
        mCamera->move(0.0f, 0.0f, -movement, mSpeed);
    }
    if (mInputState & static_cast<unsigned>(OrbitCamFlag::ORBIT_BACKWARD)) {
        mCamera->move(0.0f, 0.0f, movement, mSpeed);
    }
    if (mInputState & static_cast<unsigned>(OrbitCamFlag::ORBIT_LEFT)) {
        mCamera->move(movement, 0.0f, 0.0f, mSpeed);
    }
    if (mInputState & static_cast<unsigned>(OrbitCamFlag::ORBIT_RIGHT)) {
        mCamera->move(-movement, 0.0f, 0.0f, mSpeed);
    }
    if (mInputState & static_cast<unsigned>(OrbitCamFlag::ORBIT_UP)) {
        mCamera->move(0.0f, movement, 0.0f, mSpeed);
    }
    if (mInputState & static_cast<unsigned>(OrbitCamFlag::ORBIT_DOWN)) {
        mCamera->move(0.0f, -movement, 0.0f, mSpeed);
    }
    if (mInputState & static_cast<unsigned>(OrbitCamFlag::ORBIT_SLOW_DOWN)) {
        static constexpr float minSpeed = 0.01f;
        mSpeed *= 0.5f;
        if (mSpeed <= minSpeed) mSpeed = minSpeed;
        std::cerr << ">> OrbitCam.cc SlowDown mSpeed:" << mSpeed << '\n';
    }
    if (mInputState & static_cast<unsigned>(OrbitCamFlag::ORBIT_SPEED_UP)) {
        static constexpr float maxSpeed = 8192.0f;
        mSpeed *= 2.0f;
        if (mSpeed > maxSpeed) mSpeed = maxSpeed;
        std::cerr << ">> OrbitCam.cc SpeedUp mSpeed:" << mSpeed << '\n';        
    }

    return makeMatrix(*mCamera);
}

bool
OrbitCam::processKeyboardEvent(const KeyEvent* event)
{
    bool used = false;

    if (event->getModifiers() == QT_NoModifier) {
        used = true;
        if (event->getPress() == KeyAction_Press) {
            pickFocusPoint();

            // Check for pressed keys.
            switch (event->getKey()) {
            case Key_W:     mInputState |= static_cast<unsigned>(OrbitCamFlag::ORBIT_FORWARD);     break;
            case Key_S:     mInputState |= static_cast<unsigned>(OrbitCamFlag::ORBIT_BACKWARD);    break;
            case Key_A:     mInputState |= static_cast<unsigned>(OrbitCamFlag::ORBIT_LEFT);        break;
            case Key_D:     mInputState |= static_cast<unsigned>(OrbitCamFlag::ORBIT_RIGHT);       break;
            case Key_Space: mInputState |= static_cast<unsigned>(OrbitCamFlag::ORBIT_UP);          break;
            case Key_C:     mInputState |= static_cast<unsigned>(OrbitCamFlag::ORBIT_DOWN);        break;
            case Key_Q:     mInputState |= static_cast<unsigned>(OrbitCamFlag::ORBIT_SLOW_DOWN);   break;
            case Key_E:     mInputState |= static_cast<unsigned>(OrbitCamFlag::ORBIT_SPEED_UP);    break;
            case Key_F:     recenterCamera();                 break;
            case Key_T:     printCameraMatrices();            break;
            case Key_U:     mCamera->up = Vec3f(0.0f, 1.0f, 0.0f); break;
            case Key_R:
                if(mInitialTransformSet) {
                    clearMovementState();
                    mCamera->position = mInitialPosition;
                    mCamera->viewDir = mInitialViewDir;
                    mCamera->up = mInitialUp;
                    mCamera->focusDistance = mInitialFocusDistance;
                }
                break;
            case Key_L: mCamera->mFocusLock = !mCamera->mFocusLock; break;
            default: used = false;
            }
        } else {
            // Check for released keys.
            switch (event->getKey()) {
            case Key_W:     mInputState &= ~static_cast<unsigned>(OrbitCamFlag::ORBIT_FORWARD);    break;
            case Key_S:     mInputState &= ~static_cast<unsigned>(OrbitCamFlag::ORBIT_BACKWARD);   break;
            case Key_A:     mInputState &= ~static_cast<unsigned>(OrbitCamFlag::ORBIT_LEFT);       break;
            case Key_D:     mInputState &= ~static_cast<unsigned>(OrbitCamFlag::ORBIT_RIGHT);      break;
            case Key_Space: mInputState &= ~static_cast<unsigned>(OrbitCamFlag::ORBIT_UP);         break;
            case Key_C:     mInputState &= ~static_cast<unsigned>(OrbitCamFlag::ORBIT_DOWN);       break;
            case Key_Q:     mInputState &= ~static_cast<unsigned>(OrbitCamFlag::ORBIT_SLOW_DOWN);  break;
            case Key_E:     mInputState &= ~static_cast<unsigned>(OrbitCamFlag::ORBIT_SPEED_UP);   break;
            default: used = false;
            }
        }
    }

    return used;
}

bool
OrbitCam::processMousePressEvent(const MouseEvent* event)
{
    pickFocusPoint();

    mMouseMode = MouseMode::NONE;
    const auto buttons = event->getButtons();
    const auto modifiers = event->getModifiers();

    mMouseX = event->getX();
    mMouseY = event->getY();

    bool used = false;

    if (modifiers == QT_AltModifier) {
        if (buttons == QT_LeftButton) {
            mMouseMode = MouseMode::ORBIT;
            used = true;
        } else if (buttons == QT_MidButton) {
            mMouseMode = MouseMode::PAN;
            used = true;
        } else if (buttons == QT_RightButton) {
            mMouseMode = MouseMode::DOLLY;
            used = true;
        } else if (buttons == (QT_LeftButton | QT_RightButton)) {
            mMouseMode = MouseMode::ROLL;
            used = true;
        }
    } else if (modifiers == QT_ControlModifier) {
        if (buttons == QT_LeftButton) {
            mMouseMode = MouseMode::NONE;
            recenterCamera();
            used = true;
        }
    }

    return used;
}

bool
OrbitCam::processMouseMoveEvent(const MouseEvent* event)
{
    if (mMouseX == -1 || mMouseY == -1) {
        return false;
    }

    const int x = event->getX(); 
    const int y = event->getY(); 
    const float dClickX = float(x - mMouseX);
    const float dClickY = float(y - mMouseY);
    mMouseX = x;
    mMouseY = y;

    switch (mMouseMode) {
    case MouseMode::ORBIT:         mCamera->rotateOrbit(dClickX, dClickY); break;
    case MouseMode::PAN:           mCamera->move(dClickX, dClickY, 0.0f, mSpeed); break;
    case MouseMode::DOLLY:         mCamera->dolly(dClickX + dClickY); break;
    case MouseMode::ROLL:          mCamera->roll(dClickX); break;
    case MouseMode::ROTATE_CAMERA: mCamera->rotate(dClickX, dClickY); break;
    default: return false;
    }

    return true;
}

void
OrbitCam::clearMovementState()
{
    mInputState = 0;
    mMouseMode = MouseMode::NONE;
    mMouseX = -1;
    mMouseY = -1;
}

void
OrbitCam::recenterCamera()
{
    std::cerr << ">> OrbitCam.cc ===>> recenterCamera <<===\n";

    Vec3f newFocus;
    if (pick(newFocus)) {
        setCOI(newFocus);
    }
}

bool
OrbitCam::pick(Vec3f& hitPoint) const
{
    if (!mCalcFocusPointCallBack) return false;
    
    hitPoint = (mCalcFocusPointCallBack)();
    return (!std::isnan(hitPoint[0]) && !std::isnan(hitPoint[1]) && !std::isnan(hitPoint[2]));
}

Mat4f
OrbitCam::makeMatrix(const Camera &camera) const
{
    const Xform3f c2w = camera.camera2world();
    return Mat4f( Vec4f(c2w.l.vx.x, c2w.l.vx.y, c2w.l.vx.z, 0.0f),
                  Vec4f(c2w.l.vy.x, c2w.l.vy.y, c2w.l.vy.z, 0.0f),
                  Vec4f(c2w.l.vz.x, c2w.l.vz.y, c2w.l.vz.z, 0.0f),
                  Vec4f(c2w.p.x,    c2w.p.y,    c2w.p.z,    1.0f) );
}

void
OrbitCam::printCameraMatrices() const
{
    const Mat4f fullMat = makeMatrix(*mCamera);
    printMatrix("Full matrix containing rotation and position.", fullMat);
}

scene_rdl2::math::Mat4f 
OrbitCam::getInitialTransform() 
{ 
    Camera* const cam = new Camera();
    cam->position = mInitialPosition;
    cam->viewDir = mInitialViewDir;
    cam->up = mInitialUp;
    cam->focusDistance = mInitialFocusDistance;
    scene_rdl2::math::Mat4f mat = makeMatrix(*cam); 
    delete cam;
    return mat;
}

void
OrbitCam::setCOI(const scene_rdl2::math::Vec3f& coi)
{
    // std::cerr << ">> OrbitCam.cc setCOI coi:" << coi << '\n'; // for debug

    const Vec3f hitVec = coi - mCamera->position;
    mCamera->viewDir = normalize(hitVec);
    mCamera->up = Vec3f(0.0f, 1.0f, 0.0f);
    mCamera->focusDistance = length(hitVec);
}

scene_rdl2::math::Vec3f
OrbitCam::getCOI() const
{
    Vec3f coi = mCamera->position + mCamera->viewDir * mCamera->focusDistance;
    return coi;
}

std::string
OrbitCam::telemetryPanelInfo() const
{
    using namespace arras_render::telemetry;

    auto showFocusLock = [&]() {
        std::ostringstream ostr;
        ostr << "FocusLock:";
        if (mCamera->mFocusLock) {
            const C3 c3bg(255, 255, 0);
            const C3 c3fg = c3bg.bestContrastCol();
            ostr << c3fg.setFg() << c3bg.setBg() << "ON" << C3::resetFgBg();
        } else {
            ostr << "off";
        }
        return ostr.str();
    };

    const C3 c3bg(0, 0, 255);
    const C3 c3fg = c3bg.bestContrastCol();

    std::ostringstream ostr;
    ostr << c3fg.setFg() << c3bg.setBg() << "---- Orbit -----" << C3::resetFgBg() << '\n'
         << outF("Pos X:", mCamera->position[0]) << '\n'
         << outF("    Y:", mCamera->position[1]) << '\n'
         << outF("    Z:", mCamera->position[2]) << '\n'
         << outF("Dir X:", mCamera->viewDir[0]) << '\n'
         << outF("    Y:", mCamera->viewDir[1]) << '\n'
         << outF("    Z:", mCamera->viewDir[2]) << '\n'
         << outF(" Up X:", mCamera->up[0]) << '\n'
         << outF("    Y:", mCamera->up[1]) << '\n'
         << outF("    Z:", mCamera->up[2]) << '\n'
         << outF("Fdist:", mCamera->focusDistance) << '\n'
         << outF(" Near:", mCamera->mNear) << '\n'
         << outF("Speed:", mSpeed) << '\n'
         << showFocusLock();

    return ostr.str();
}

} // namespace arras_render
