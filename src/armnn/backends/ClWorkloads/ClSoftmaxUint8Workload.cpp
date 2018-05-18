//
// Copyright © 2017 Arm Ltd. All rights reserved.
// See LICENSE file in the project root for full license information.
//

#include "ClSoftmaxUint8Workload.hpp"
#include "backends/ClTensorHandle.hpp"
#include "backends/CpuTensorHandle.hpp"

namespace armnn
{

ClSoftmaxUint8Workload::ClSoftmaxUint8Workload(const SoftmaxQueueDescriptor& descriptor, const WorkloadInfo& info,
                                               std::shared_ptr<arm_compute::MemoryManagerOnDemand>& memoryManager)
    : Uint8Workload<SoftmaxQueueDescriptor>(descriptor, info)
    , m_SoftmaxLayer(memoryManager)
{
    m_Data.ValidateInputsOutputs("ClSoftmaxUint8Workload", 1, 1);

    arm_compute::ICLTensor& input  = static_cast<ClTensorHandle*>(m_Data.m_Inputs[0])->GetTensor();
    arm_compute::ICLTensor& output = static_cast<ClTensorHandle*>(m_Data.m_Outputs[0])->GetTensor();

    const auto outputQuantization = output.info()->quantization_info();

    if ((outputQuantization.scale != (1.0f / 256.0f)) || (outputQuantization.offset != 0))
    {
        throw InvalidArgumentException(
            "Invalid quantization for output. Only scale = 1.0f / 256.0f and offset = 0 supported");
    }

    m_SoftmaxLayer.configure(&input, &output, descriptor.m_Parameters.m_Beta);
}

void ClSoftmaxUint8Workload::Execute() const
{
    ARMNN_SCOPED_PROFILING_EVENT(Compute::GpuAcc, "ClSoftmaxUint8Workload_Execute");

    m_SoftmaxLayer.run();
}

} //namespace armnn
