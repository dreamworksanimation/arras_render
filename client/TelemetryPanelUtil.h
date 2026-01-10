// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <scene_rdl2/common/math/Vec3.h>

#include <iomanip>
#include <sstream>

namespace arras_render {
namespace telemetry {

class C3
//
// 8bit RGB Color definition for TelemetryOverlay
//
{
public:
    C3() = default;
    C3(const unsigned char r, const unsigned char g, const unsigned char b) : mR(r), mG(g), mB(b) {}
    C3(const C3& c3) : mR(c3.mR), mG(c3.mG), mB(c3.mB) {}

    bool isBlack() const { return (mR == 0x0 && mG == 0x0 && mB == 0x0); }

    C3
    bestContrastCol() const
    {
        const float l = luminance();
        const float contrastWhite = (1.0f + 0.05f) / (l + 0.05f);
        const float contrastBlack = (l + 0.05f) / (0.0f + 0.05f);
        return (contrastWhite > contrastBlack) ? C3(255, 255, 255) : C3(0, 0, 0);
    }

    float
    luminance() const
    {
        return (0.2126f * static_cast<float>(mR) / 255.0f +
                0.7152f * static_cast<float>(mG) / 255.0f +
                0.0722f * static_cast<float>(mB) / 255.0f);
    }

    std::string setFg() const { return colStrEscapeSequence(true); }
    std::string setBg() const { return colStrEscapeSequence(false); }

    static std::string resetFgBg()
    {
        const C3 cFg(255, 255, 255);
        const C3 cBg(0, 0, 0);
        return cFg.setFg() + cBg.setBg();
    }

    unsigned char mR {0};
    unsigned char mG {0};
    unsigned char mB {0};

private:
    std::string colStrEscapeSequence(const bool fg) const
    {
        // \e[38;2;R;G;Bm : define fg color escape sequence
        // \e[48;2;R;G;Bm : define bg color escape sequence
        std::ostringstream ostr;
        ostr << "\e[" << (fg ? "38" : "48") << ";2;"
             << std::to_string(static_cast<unsigned>(mR)) << ';'
             << std::to_string(static_cast<unsigned>(mG)) << ';'
             << std::to_string(static_cast<unsigned>(mB)) << 'm';
        return ostr.str();
    }
};

inline std::string
outF(const std::string& msg, const float v)
{
    std::ostringstream ostr;
    ostr << msg << std::setw(10) << std::fixed << std::setprecision(5) << v;
    return ostr.str();
}

inline std::string
outBool(const std::string& msg, const bool b)
{
    std::ostringstream ostr;
    ostr << msg << (b ? "on " : "off");
    return ostr.str();
}

} // namespace telemetry
} // namespace arras_render
