// Copyright 2023-2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "NavigationCam.h"

#include <sstream>

namespace arras_render {

std::string
mouseButtonStr(const int code)
{
    if (code == QT_NoButton) return "QT_NoButton";

    std::ostringstream ostr;
    if (code & QT_LeftButton) ostr << "QT_LeftButton ";
    if (code & QT_RightButton) ostr << "QT_RightButton ";
    if (code & QT_MidButton) ostr << "QT_MidButton ";
    return ostr.str();
}

std::string
keyModifierCodeStr(const int code)
{
    if (code == QT_NoModifier) return "QT_NoModifier";

    std::ostringstream ostr;
    if (code & QT_ALT) ostr << "QT_ALT ";
    if (code & QT_SHIFT) ostr << "QT_SHIFT ";
    if (code & QT_META) ostr << "QT_META ";
    if (code & QT_CTRL) ostr << "QT_CTRL ";
    return ostr.str();
}

std::string
keyEventCodeStr(const int keyCode)
{
    switch (keyCode) {
    case Key_A : return "Key_A";
    case Key_B : return "Key_B";
    case Key_C : return "Key_C";
    case Key_D : return "Key_D";
    case Key_E : return "Key_E";
    case Key_F : return "Key_F";
    case Key_G : return "Key_G";
    case Key_H : return "Key_H";
    case Key_I : return "Key_I";
    case Key_J : return "Key_J";
    case Key_K : return "Key_K";
    case Key_L : return "Key_L";
    case Key_M : return "Key_M";
    case Key_N : return "Key_N";
    case Key_O : return "Key_O";
    case Key_P : return "Key_P";
    case Key_Q : return "Key_Q";
    case Key_R : return "Key_R";
    case Key_S : return "Key_S";
    case Key_T : return "Key_T";
    case Key_U : return "Key_U";
    case Key_V : return "Key_V";
    case Key_W : return "Key_W";
    case Key_X : return "Key_X";
    case Key_Y : return "Key_Y";
    case Key_Z : return "Key_Z";

    case Key_0 : return "Key_0";
    case Key_1 : return "Key_1";
    case Key_2 : return "Key_2";
    case Key_3 : return "Key_3";
    case Key_4 : return "Key_4";
    case Key_5 : return "Key_5";
    case Key_6 : return "Key_6";
    case Key_7 : return "Key_7";
    case Key_8 : return "Key_8";
    case Key_9 : return "Key_9";

    case Key_ESC : return "Key_ESC"; // esc
    case Key_GRAVE : return "Key_GRAVE"; // `
    case Key_MINUS : return "Key_MINUS"; // -
    case Key_EQUAL : return "Key_EQUAL"; // =
    case Key_DELETE : return "Key_DELETE"; // delete
    case Key_SQUAREBRACKET_OPEN : return "Key_SQUAREBRACKET_OPEN"; // [
    case Key_SQUAREBRACKET_CLOSE : return "Key_SQUAREBRACKET_CLOSE"; // ]
    case Key_BACKSLASH : return "Key_BACKSLASH";
    case Key_SHIFT : return "Key_SHIFT";
    case Key_ALT : return "Key_ALT";
    case Key_CTRL : return "Key_CTRL";
    case Key_CAPSLOCK : return "Key_CAPSLOCK"; // capslock
    case Key_SEMICOLON : return "Key_SEMICOLON"; // ;
    case Key_APOSTROPHE : return "Key_APOSTROPHE"; // '
    case Key_Enter : return "Key_Enter";
    case Key_COMMA : return "Key_COMMA"; // ,
    case Key_DOT : return "Key_DOT"; // .
    case Key_SLASH : return "Key_SLASH"; // /

    case Key_Space : return "Key_Space";

    default : return "?";
    }
}

std::string
keyActionCodeStr(const int code)
{
    switch (code) {
    case KeyAction_Press : return "KeyAction_Press";
    case KeyAction_Release : return "KeyAction_Release";
    default : return "?";
    }
}

std::string
KeyEvent::show() const
{
    std::ostringstream ostr;
    ostr << "KeyEvent {\n"
         << "  mKey:" << mKey
         << " (0x" << std::hex << mKey << std::dec << ":" << keyEventCodeStr(mKey) << ")\n"
         << "  mPress:" << mPress << " (" << keyActionCodeStr(mPress) << ")\n"
         << "  mModifier:" << mModifier
         << " (0x" << std::hex << mModifier << std::dec << ":" << keyModifierCodeStr(mModifier) << ")\n"
         << "}";
    return ostr.str();
}

//------------------------------------------------------------------------------------------

std::string
MouseEvent::show() const
{
    std::ostringstream ostr;
    ostr << "MoseEvent {\n"
         << "  mX:" << mX << '\n'
         << "  mY:" << mY << '\n'
         << "  mModifier:" << mModifier << ' ' << keyModifierCodeStr(mModifier) << '\n'
         << "  mButton:" << mButton << ' ' << mouseButtonStr(mButton) << '\n'
         << "  mButtons:" << mButtons << ' ' << mouseButtonStr(mButtons) << '\n'
         << "}";
    return ostr.str();
}

} // namespace arras_render
