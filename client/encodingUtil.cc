// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "encodingUtil.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>

namespace {
// beauty is always 4 channels with ClientReceiverFb
unsigned short constexpr NUM_BTY_CHANNELS = 4;
}

namespace arras_render {


void
writeBuffersToExr(const std::string& exrFileName,
                  const std::vector<OIIO::ImageSpec>& specs,
                  const std::vector<std::vector<float>>& buffers)
{
    assert(specs.size() == buffers.size());
    std::unique_ptr<OIIO::ImageOutput> out(OIIO::ImageOutput::create(exrFileName));

    out->open(exrFileName, static_cast<int>(specs.size()), specs.data());
    for (size_t i = 0; i < specs.size(); ++i) {
        if (i > 0) { // first spec is already opened
            out->open(exrFileName, specs[i], OIIO::ImageOutput::AppendSubimage);
        }

        out->write_image(specs[i].format, buffers[i].data());
    }

    out->close();
}


void
writeExrFile(const std::string& exrFileName, mcrt_dataio::ClientReceiverFb& fbReceiver)
{
    const unsigned int width = fbReceiver.getWidth();
    const unsigned int height = fbReceiver.getHeight();

    std::vector<OIIO::ImageSpec> specs;
    std::vector<std::vector<float>> buffers;

    specs.emplace_back(width, height, NUM_BTY_CHANNELS, OIIO::TypeDesc::FLOAT);

    OIIO::ImageSpec& spec = specs.back();
    spec.attribute("subimagename", "beauty");
    spec.attribute("name", "beauty");

    buffers.emplace_back(width * height * NUM_BTY_CHANNELS);
    fbReceiver.getBeauty(buffers.back(), true);

    for (unsigned i = 0; i < fbReceiver.getTotalRenderOutput(); ++i) {
        const int numChannels = fbReceiver.getRenderOutputNumChan(i);

        specs.emplace_back(width, height, numChannels, OIIO::TypeDesc::FLOAT);
        OIIO::ImageSpec& spec = specs.back();

        const std::string& outputName = fbReceiver.getRenderOutputName(i);
        spec.attribute("subimagename", outputName);
        spec.attribute("name", outputName);

        buffers.emplace_back(width * height * numChannels);
        fbReceiver.getRenderOutput(i, buffers.back(),
                                   true, // top2bottom
                                   false); // closestFilterDepthOutput
    }

    std::cout << "writing to file" << std::endl;
    writeBuffersToExr(exrFileName, specs, buffers);
}

} // end namespace
