// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <scene_rdl2/common/math/Mat4.h>

namespace moonray { namespace rndr { class RenderContext; } }

enum CameraType
{
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

const int QT_AltModifier    = 0x08000000;
const int QT_NoModifier	    = 0x00000000; //No modifier key is pressed.
const int QT_SHIFT 			= 0x02000000; //	The Shift keys provided on all standard keyboards.
const int QT_ControlModifier= 0x04000000;
const int QT_META 			= 0x10000000; //	The Meta keys.
const int QT_CTRL 			= 0x04000000; //	The Ctrl keys.
const int QT_ALT 			= 0x08000000; //The normal Alt keys, but not keys like AltGr.
const int QT_UNICODE_ACCEL 	= 0x00000000; //	The shortcut is specified as a Unicode code point, not as a Qt Key.
const int QT_NoButton 		= 0x00000000; //	The button state does not refer to any button (see QMouseEvent::button()).
const int QT_LeftButton		= 0x00000001; //	The left button is pressed, or an event refers to the left button. (The left button may be the right button on left-handed mice.)
const int QT_RightButton 	= 0x00000002; //	The right button.
const int QT_MidButton 		= 0x00000004; //	The middle button.
//const int QT_MiddleButton 	= MidButton; //	The middle button.  <-- online documentation does not define this,  need to look in header files... b.stastny
const int QT_XButton1 		= 0x00000008; //	The first X button.
const int QT_XButton2 		= 0x00000010; //	The second X button.

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

const int Key_APOSTROPHE         = 0x27; // '
const int Key_SEMICOLON          = 0x3b; // ;
const int Key_SQUAREBRACKET_OPEN = 0x5b; // [
const int Key_SLASH              = 0x2f; // /

const int Key_Space = 0x20;
const int Key_Enter = 0x01000004;

const int Press = 1;
const int Release = 0;

class KeyEvent
{
public:
    KeyEvent(int type, int key, int mod )
	{
    	mPress = type;
    	k = key;
    	mModifier = mod;
	}
    ~KeyEvent() {};

    int key() const { return k; }

    int modifiers() { return mModifier; }

public:
    int k;
    int mPress;
    int mModifier;

};

class MouseEvent {
public:
	MouseEvent( int x, int y, int modifier, int button, int buttons ) {
		mX = x;
		mY = y;
		mModifier = modifier;
		mButton = button;
		mButtons = buttons;
	}
	~MouseEvent() {}

	int buttons() { return mButton; }  // not sure what the difference is between button and buttons yet....
	int button() { return mButton; }
	int modifiers() { return mModifier;}
	int x() { return mX; }
	int y() { return mY; }

    std::string show() const;

public:

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
    virtual             ~NavigationCam() {}

    // Certain types of camera may want to intersect with the scene, in which
    // case they'll need more information about the scene. This function does
    // nothing by default.
    virtual void        setRenderContext(const moonray::rndr::RenderContext &context) {}

    // If this camera model imposes any constraints on the input matrix, then
    // the constrained matrix is returned, otherwise the output will equal in 
    // input.
    // If makeDefault is set to true then this xform is designated a the new
    // default transform when/if the camera is reset.
    virtual scene_rdl2::math::Mat4f resetTransform(const scene_rdl2::math::Mat4f &xform, bool makeDefault) = 0;

    // Returns the latest camera matrix.
    virtual scene_rdl2::math::Mat4f update(float dt) = 0;

    /// Returns true if the input was used, false to pass the input to a higher
    /// level handler.
    virtual bool        processKeyboardEvent(KeyEvent *event, bool pressed) { return false; }
    virtual bool        processMousePressEvent(MouseEvent *event) { return false; }
    virtual bool        processMouseReleaseEvent(MouseEvent *event) { return false; }
    virtual bool        processMouseMoveEvent(MouseEvent *event) { return false; }
    virtual void        clearMovementState() {};
};
