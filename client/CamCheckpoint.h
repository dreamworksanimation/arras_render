// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <scene_rdl2/common/math/Mat4.h>

namespace arras_render {
namespace telemetry {

class CamCheckpoint
//
// This class keeps multiple versions of the camera matrix and maintains them as a sequential
// operation history.
//
{
public:
    using Mat4f = scene_rdl2::math::Mat4f;

    CamCheckpoint() {};

    void updatePathVisCamMtx(const Mat4f& mtx);

    void update(const Mat4f& mtx);
    void push(const Mat4f& mtx);

    bool isEmpty() const { return mMtxTbl.empty(); }
    Mat4f getCurr() const;
    Mat4f getCurrPathVisCamMtx() const { return mPathVisCamMtx; }
    Mat4f getPrev();
    Mat4f getNext();

    Mat4f swapBetweenCurrAndPathVisCam();

    bool isCurrCamPathVisCam() const { return isSameMtx(getCurr(), mPathVisCamMtx); }

    std::string telemetryPanelInfo() const;

    std::string show() const;

private:
    bool isValidId(const unsigned id) const { return id < mMtxTbl.size(); }

    bool isSameMtx(const Mat4f& a, const Mat4f& b) const;

    Mat4f mPathVisCamMtx;
    
    bool mKeepMtxValid {false};
    Mat4f mKeepMtx;

    unsigned mCurrId {0};
    std::vector<Mat4f> mMtxTbl;
};

} // namespace telemetry
} // namespace arras_render
