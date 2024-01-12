// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "NavigationCam.h"

#include <sstream>

std::string
MouseEvent::show() const
{
    auto showModifier = [](const int v) -> std::string {
        if (v == QT_NoModifier) return "QT_NoModifier";

        std::ostringstream ostr;
        if (v & QT_AltModifier) ostr << "QT_AltModifier ";
        if (v & QT_META) ostr << "QT_META ";
        return ostr.str();
    };
    auto showButton = [](const int v) -> std::string {
        if (v == QT_NoButton) return "QT_NoButton";

        std::ostringstream ostr;
        if (v & QT_LeftButton) ostr << "QT_LeftButton ";
        if (v & QT_RightButton) ostr << "QT_RithtButton ";
        if (v & QT_MidButton) ostr << "QT_MidButton ";
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "MoseEvent {\n"
         << "  mX:" << mX << '\n'
         << "  mY:" << mY << '\n'
         << "  mModifier:" << mModifier << ' ' << showModifier(mModifier) << '\n'
         << "  mButton:" << mButton << ' ' << showButton(mButton) << '\n'
         << "  mButtons:" << mButtons << ' ' << showButton(mButtons) << '\n'
         << "}";
    return ostr.str();
}
