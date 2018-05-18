//
// Copyright © 2017 Arm Ltd. All rights reserved.
// See LICENSE file in the project root for full license information.
//

#pragma once

#include <backends/NeonWorkloadUtils.hpp>

#include "arm_compute/runtime/MemoryManagerOnDemand.h"

namespace armnn
{

class NeonNormalizationFloat32Workload : public Float32Workload<NormalizationQueueDescriptor>
{
public:
    NeonNormalizationFloat32Workload(const NormalizationQueueDescriptor& descriptor, const WorkloadInfo& info,
                                     std::shared_ptr<arm_compute::MemoryManagerOnDemand>& memoryManager);
    virtual void Execute() const override;

private:
    mutable arm_compute::NENormalizationLayer m_NormalizationLayer;
};

} //namespace armnn




