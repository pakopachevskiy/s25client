// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AIPlayerJH.h"

#include "ai/AIInterface.h"
#include "ai/aijh/debug/AIPerfReporter.h"
#include "ai/aijh/debug/AIStatsReporter.h"

namespace AIJH {

void AIPlayerJH::saveStats(unsigned gf) const
{
    statsReporter_->SaveStats(gf);
}

void AIPlayerJH::saveDebugStats(unsigned gf) const
{
    statsReporter_->SaveDebugStats(gf);
}

void AIPlayerJH::RecordGlobalPositionSearchInvocation()
{
    ++globalPositionSearchInvocations_;
}

void AIPlayerJH::RecordGlobalPositionSearchCooldownSkip()
{
    ++globalPositionSearchCooldownSkips_;
}

unsigned long long AIPlayerJH::GetResourceValueCacheHits() const
{
    return aii.Queries().GetResourceValueCacheHits();
}

unsigned long long AIPlayerJH::GetResourceValueCacheMisses() const
{
    return aii.Queries().GetResourceValueCacheMisses();
}

} // namespace AIJH
