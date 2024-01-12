// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#ifndef OUTPUT_RATE_H
#define OUTPUT_RATE_H

#include <string>

#include <sdk/sdk.h>

namespace arras_render {

void setOutputRate(arras4::sdk::SDK& sdk,
                   unsigned interval,
                   unsigned offset=1,
                   std::string priorityAov=std::string(),
                   unsigned priorityInterval=1);

}

#endif /* OUTPUT_RATE_H */
