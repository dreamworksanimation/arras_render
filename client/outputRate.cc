// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "outputRate.h"

#include <mcrt_messages/OutputRates.h>

namespace arras_render {

void
setOutputRate(arras4::sdk::SDK& sdk,
              unsigned interval,
              unsigned offset,
              std::string priorityAov,
              unsigned priorityInterval)
{
    mcrt::OutputRates rates;
    rates.setDefaultRate(interval, offset);

    if (priorityAov != std::string()) {
        rates.setRate(priorityAov, priorityInterval, priorityInterval);
    }

    rates.setSendAllWhenComplete(true);

    sdk.sendMessage(rates.getAsMessage());
}

} // end namespace
