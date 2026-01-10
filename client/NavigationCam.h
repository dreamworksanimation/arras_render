// Copyright 2023-2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <scene_rdl2/common/math/Mat4.h>

namespace moonray { namespace rndr { class RenderContext; } }

namespace arras_render {

enum class CameraType : int {
    ORBIT_CAM,
    FREE_CAM,
    NUM_CAMERA_TYPES,
};

// Can't pull QT into a computation because it needs more static memory then the TLS supports
// google Fu show that we could recompile some off the gcc libs to add more space but that would impact
// the entire project / team so since this is prototype work we will hobble around it and there are tight
// deadlines... b.stastny

const int QT_PRESS = 1;
const int QT_RELEASE = 2;
const int QT_MOVE = 3;

//------------------------------    

const int QT_RightButton 	= 0x00000002; // The mouse right button.
const int QT_MidButton 		= 0x00000004; // The mouse middle button.
const int QT_XButton1 		= 0x00000008; // The mouse first X button.
const int QT_XButton2 		= 0x00000010; // The mouse second X button.
const int QT_LeftButton		= 0x00000001; // The left button is pressed, or an event refers to the left button.
                                          // (The left button may be the right button on left-handed mice.)

std::string mouseButtonStr(const int code);

//------------------------------

const int QT_AltModifier    = 0x08000000;
const int QT_NoModifier	    = 0x00000000; // No modifier key is pressed.
const int QT_SHIFT 			= 0x02000000; // The Shift keys provided on all standard keyboards.
const int QT_ControlModifier= 0x04000000;
const int QT_META 			= 0x10000000; // The Meta keys.
const int QT_CTRL 			= 0x04000000; // The Ctrl keys.
const int QT_ALT 			= 0x08000000; // The normal Alt keys, but not keys like AltGr.
const int QT_UNICODE_ACCEL 	= 0x00000000; // The shortcut is specified as a Unicode code point, not as a Qt Key.
const int QT_NoButton 		= 0x00000000; // The button state does not refer to any button (see QMouseEvent::button()).
    
std::string keyModifierCodeStr(const int code);

//------------------------------

const int Key_A = 0x41;
const int Key_B = 0x42;
const int Key_C = 0x43;
const int Key_D = 0x44;
const int Key_E = 0x45;
const int Key_F = 0x46;
const int Key_G = 0x47;
const int Key_H = 0x48;
const int Key_I = 0x49;
const int Key_J = 0x4a;
const int Key_K = 0x4b;
const int Key_L = 0x4c;
const int Key_M = 0x4d;
const int Key_N = 0x4e;
const int Key_O = 0x4f;
const int Key_P = 0x50;
const int Key_Q = 0x51;
const int Key_R = 0x52;
const int Key_S = 0x53;
const int Key_T = 0x54;
const int Key_U = 0x55;
const int Key_V = 0x56;
const int Key_W = 0x57;
const int Key_X = 0x58;
const int Key_Y = 0x59;
const int Key_Z = 0x5a;

const int Key_0 = 0x30;
const int Key_1 = 0x31;
const int Key_2 = 0x32;
const int Key_3 = 0x33;
const int Key_4 = 0x34;
const int Key_5 = 0x35;
const int Key_6 = 0x36;
const int Key_7 = 0x37;
const int Key_8 = 0x38;
const int Key_9 = 0x39;

const int Key_ESC = 0x1000000; // esc
const int Key_GRAVE = 0x60; // `
const int Key_MINUS = 0x2d; // -
const int Key_EQUAL = 0x3d; // =
const int Key_DELETE = 0x1000003; // delete
const int Key_SQUAREBRACKET_OPEN = 0x5b; // [
const int Key_SQUAREBRACKET_CLOSE = 0x5d; // ]
const int Key_BACKSLASH = 0x5c;
const int Key_SHIFT = 0x1000020; // shift
const int Key_ALT = 0x1000021; // alt
const int Key_CTRL = 0x1000023; // ctrl
const int Key_CAPSLOCK = 0x1000024; // capslock
const int Key_SEMICOLON = 0x3b; // ;
const int Key_APOSTROPHE = 0x27; // '
const int Key_COMMA = 0x2c; // ,
const int Key_DOT = 0x2e; // .
const int Key_SLASH = 0x2f; // /
const int Key_QUESTION = 0x3f; // ?

const int Key_Space = 0x20;
const int Key_Enter = 0x01000004;

std::string keyEventCodeStr(const int keyCode);

//------------------------------

const int KeyAction_Press = 1;
const int KeyAction_Release = 0;

std::string keyActionCodeStr(const int code);

//------------------------------------------------------------------------------------------

class KeyEvent
{
public:
    KeyEvent(const int press, const int key, const int modifier, const bool autoRepeat)
        : mKey {key}
        , mPress {press}
        , mModifier {modifier}
        , mAutoRepeat {autoRepeat}
	{}
    ~KeyEvent() {};

    int getKey() const { return mKey; }
    int getPress() const { return mPress; }
    int getModifiers() const { return mModifier; }
    bool getAutoRepeat() const { return mAutoRepeat; }

    std::string show() const;

private:
    int mKey;
    int mPress;
    int mModifier;
    bool mAutoRepeat {false};
};

class MouseEvent {
public:
	MouseEvent( const int x, const int y, const int modifier, const int button, const int buttons )
        : mX {x}
        , mY {y}
        , mModifier {modifier}
        , mButton {button}
        , mButtons {buttons}
    {}
	~MouseEvent() {}

	int getX() const { return mX; }
	int getY() const { return mY; }
	int getModifiers() const { return mModifier;}
	int getButton() const { return mButton; }
	int getButtons() const { return mButton; }  // not sure what the difference is between button and buttons yet....

    std::string show() const;

private:
	int mX;
	int mY;
	int mModifier;
	int mButton;
	int mButtons;
};

///
/// Pure virtual base class which further navigation models may be implemented
/// on top of.
///
class NavigationCam
{
public:
    NavigationCam() {}
    virtual ~NavigationCam() {}

    // Certain types of camera may want to intersect with the scene, in which
    // case they'll need more information about the scene. This function does
    // nothing by default.
    virtual void setRenderContext(const moonray::rndr::RenderContext &context) {}

    // If this camera model imposes any constraints on the input matrix, then
    // the constrained matrix is returned, otherwise the output will equal in 
    // input.
    // If makeDefault is set to true then this xform is designated a the new
    // default transform when/if the camera is reset.
    virtual scene_rdl2::math::Mat4f resetTransform(const scene_rdl2::math::Mat4f &xform,
                                                   const bool makeDefault) = 0;

    // Returns the latest camera matrix.
    virtual scene_rdl2::math::Mat4f update(const float dt) = 0;

    /// Returns true if the input was used, false to pass the input to a higher
    /// level handler.
    virtual bool processKeyboardEvent(const KeyEvent *event) { return false; }
    virtual bool processMousePressEvent(const MouseEvent *event) { return false; }
    virtual bool processMouseReleaseEvent(const MouseEvent *event) { return false; }
    virtual bool processMouseMoveEvent(const MouseEvent *event) { return false; }
    virtual void clearMovementState() {};
};

} // namespace arras_render
