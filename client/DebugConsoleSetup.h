// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mcrt_dataio/client/receiver/ClientReceiverConsoleDriver.h>
#include <mcrt_dataio/client/receiver/ClientReceiverFb.h>
#include <sdk/sdk.h>

#include <atomic>
#include <memory>

class ImageView;

namespace arras_render {

void
debugConsoleSetup(int port,
                  std::shared_ptr<arras4::sdk::SDK> &sdk,
                  std::shared_ptr<mcrt_dataio::ClientReceiverFb> &fbReceiver,
                  std::atomic<ImageView *> &imageView);

} // namespace arras_render
