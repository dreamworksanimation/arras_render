// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#ifndef ENCODING_UTIL_H
#define ENCODING_UTIL_H

#include <string>

#include <mcrt_dataio/client/receiver/ClientReceiverFb.h>

namespace arras_render {

void
writeExrFile(const std::string& exrFileName, mcrt_dataio::ClientReceiverFb& fbReceiver);

}

#endif /* ENCODING_UTIL_H */
