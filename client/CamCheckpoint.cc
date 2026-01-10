// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "CamCheckpoint.h"
#include "TelemetryPanelUtil.h"

#include <scene_rdl2/render/util/StrUtil.h>

#include <iomanip>

namespace arras_render {
namespace telemetry {

void
CamCheckpoint::updatePathVisCamMtx(const Mat4f& mtx)
{
    mPathVisCamMtx = mtx;
}

void
CamCheckpoint::update(const Mat4f& mtx)
{
    if (mMtxTbl.empty()) {
        push(mtx);
        return;
    }
    mMtxTbl[mCurrId] = mtx;
}

void
CamCheckpoint::push(const Mat4f& mtx)
{
    if (mMtxTbl.empty()) {
        mMtxTbl.push_back(mtx); // [0] add
        mMtxTbl.push_back(mtx); // [1] add <- current
        updatePathVisCamMtx(mtx);
        mCurrId = 1;
        return;
    }

    if (mCurrId == mMtxTbl.size() - 1) {
        mCurrId++;
        mMtxTbl[mCurrId - 1] = mtx; // [mCurrId - 1] update
        mMtxTbl.push_back(mtx);     // [mCurrId]     add <- current
    } else if (mCurrId < mMtxTbl.size() - 1) {
        mCurrId++;
        mMtxTbl[mCurrId - 1] = mtx; // [mCurrId - 1] update
        mMtxTbl.resize(mCurrId);
        mMtxTbl.push_back(mtx);     // [mCurrId]     add <- current 
    }
}

CamCheckpoint::Mat4f
CamCheckpoint::getCurr() const
{
    if (!isValidId(mCurrId)) return Mat4f();
    return mMtxTbl[mCurrId];
}

CamCheckpoint::Mat4f
CamCheckpoint::getPrev()
{
    if (mCurrId > 0) --mCurrId;
    return mMtxTbl[mCurrId];
}

CamCheckpoint::Mat4f
CamCheckpoint::getNext()
{
    if (mCurrId < mMtxTbl.size() - 1) ++mCurrId;
    return mMtxTbl[mCurrId];
}

CamCheckpoint::Mat4f
CamCheckpoint::swapBetweenCurrAndPathVisCam()
{
    if (isCurrCamPathVisCam()) {
        // currMtx == pathVisCamMtx
        if (mKeepMtxValid) {
            mKeepMtxValid = false;
            update(mKeepMtx);
        }
    } else {
        // currMtx != pathVisCamMtx
        mKeepMtx = getCurr();
        mKeepMtxValid = true;
        update(mPathVisCamMtx);
    }
    return getCurr();
}

std::string
CamCheckpoint::telemetryPanelInfo() const
{
    auto showCurrBar = [&]() {
        std::ostringstream ostr;
        const C3 bgCol(255,255,0);
        const C3 fgCol = bgCol.bestContrastCol();
        for (size_t i = 0; i < mMtxTbl.size(); ++i) {
            if (i == static_cast<size_t>(mCurrId)) {
                ostr << fgCol.setFg() << bgCol.setBg() << '*' << C3::resetFgBg();
            } else {
                ostr << '-';
            }
        }
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "CamType:";
    if (isCurrCamPathVisCam()) {
        const C3 bgCol(0,0,255);
        const C3 fgCol = bgCol.bestContrastCol();
        ostr << fgCol.setFg() << bgCol.setBg() << "PathVisCam" << C3::resetFgBg() << '\n';
    } else {
        const C3 bgCol(255,255,255);
        const C3 fgCol = bgCol.bestContrastCol();
        ostr << fgCol.setFg() << bgCol.setBg() << "interactive" << C3::resetFgBg() << '\n';
    }

    ostr << "CamChkpnt:";
    if (mMtxTbl.empty()) {
        return "EMPTY";
    }

    const size_t maxId = mMtxTbl.size() - 1;
    const int w = scene_rdl2::str_util::getNumberOfDigits(maxId);

    ostr << std::setw(w) << mCurrId << '/' << mMtxTbl.size() - 1 << '\n'
         << showCurrBar();
    return ostr.str();
}

std::string
CamCheckpoint::show() const
{
    auto showMtx = [](const Mat4f& mtx) {
        auto showV = [](const float v) {
            std::ostringstream ostr;
            ostr << std::setw(10) << std::fixed << std::setprecision(5) << v;
            return ostr.str();
        };
        std::ostringstream ostr;
        ostr << "mtx {\n"
             << "  " << showV(mtx.vx[0]) << ',' << showV(mtx.vx[1]) << ',' << showV(mtx.vx[2]) << ',' << showV(mtx.vx[3]) << '\n'
             << "  " << showV(mtx.vy[0]) << ',' << showV(mtx.vy[1]) << ',' << showV(mtx.vy[2]) << ',' << showV(mtx.vy[3]) << '\n'
             << "  " << showV(mtx.vz[0]) << ',' << showV(mtx.vz[1]) << ',' << showV(mtx.vz[2]) << ',' << showV(mtx.vz[3]) << '\n'
             << "  " << showV(mtx.vw[0]) << ',' << showV(mtx.vw[1]) << ',' << showV(mtx.vw[2]) << ',' << showV(mtx.vw[3]) << '\n'
             << "}";
        return ostr.str();
    };

    const int wi = scene_rdl2::str_util::getNumberOfDigits(mMtxTbl.size());

    std::ostringstream ostr;
    ostr << "CamCheckpoint (size:" << mMtxTbl.size() << ") mCurrId:" << mCurrId << " {\n";
    for (size_t i = 0; i < mMtxTbl.size(); ++i) {
        ostr << scene_rdl2::str_util::addIndent("i:" + std::to_string(i) + ' ' + showMtx(mMtxTbl[i]));
        if (i == mCurrId) {
            ostr << " <<== mCurrId:" << mCurrId << '\n';
        } else {
            ostr << '\n';
        }
    }
    ostr << "}";
    return ostr.str();
}

bool
CamCheckpoint::isSameMtx(const Mat4f& a, const Mat4f& b) const
{
    //
    // Actually, Mat4 does have an == operator, so it could have been used here. However, there is a reason
    // why we deliberately use a custom isSameMtx function in this context. If you look carefully at this if
    // statement, you'll notice that the comparison deliberately starts with the translate components. This
    // is because, in most cases, camera operations in the light path visualizer involve changes in the
    // camera position. By comparing the position components first, the updated status of the matrix can
    // usually be determined just by checking the first three values. Since this check for matrix updates
    // is expected to be performed quite frequently, we are hoping for improved execution speed.
    //
    return (a.vw[0] == b.vw[0] && a.vw[1] == b.vw[1] && a.vw[2] == b.vw[2] && a.vw[3] == b.vw[3] &&
            a.vx[0] == b.vx[0] && a.vx[1] == b.vx[1] && a.vx[2] == b.vx[2] && a.vx[3] == b.vx[3] &&
            a.vy[0] == b.vy[0] && a.vy[1] == b.vy[1] && a.vy[2] == b.vy[2] && a.vy[3] == b.vy[3] &&
            a.vz[0] == b.vz[0] && a.vz[1] == b.vz[1] && a.vz[2] == b.vz[2] && a.vz[3] == b.vz[3]);
}

} // namespace telemetry
} // namespace arras_render
