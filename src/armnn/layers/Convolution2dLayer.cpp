//
// Copyright © 2017 Arm Ltd. All rights reserved.
// See LICENSE file in the project root for full license information.
//
#include "Convolution2dLayer.hpp"

#include "LayerCloneBase.hpp"

#include <armnn/TypesUtils.hpp>
#include <backends/CpuTensorHandle.hpp>
#include <backends/WorkloadFactory.hpp>

namespace armnn
{

Convolution2dLayer::Convolution2dLayer(const Convolution2dDescriptor& param, const char* name)
    : LayerWithParameters(1, 1, LayerType::Convolution2d, param, name)
{
}

std::unique_ptr<IWorkload> Convolution2dLayer::CreateWorkload(const Graph& graph, const IWorkloadFactory& factory) const
{
    Convolution2dQueueDescriptor descriptor;

    descriptor.m_Weight = m_Weight.get();
    if (m_Param.m_BiasEnabled)
    {
        descriptor.m_Bias = m_Bias.get();
    }
    return factory.CreateConvolution2d(descriptor, PrepInfoAndDesc(descriptor, graph));
}

Convolution2dLayer* Convolution2dLayer::Clone(Graph& graph) const
{
    auto layer = CloneBase<Convolution2dLayer>(graph, m_Param, GetName());
    layer->m_Weight = m_Weight ? std::make_unique<ScopedCpuTensorHandle>(*m_Weight) : nullptr;

    if (layer->m_Param.m_BiasEnabled)
    {
        layer->m_Bias = m_Bias ? std::make_unique<ScopedCpuTensorHandle>(*m_Bias) : nullptr;
    }

    return std::move(layer);
}

void Convolution2dLayer::ValidateTensorShapesFromInputs()
{
    ConditionalThrow<LayerValidationException>(GetInputSlot(0).GetConnection() != nullptr,
                     "Convolution2dLayer: InputSlot must be connected to an OutputSlot");
    ConditionalThrow<LayerValidationException>(GetInputSlot(0).GetConnection()->IsTensorInfoSet(),
                     "Convolution2dLayer: TensorInfo must be set on connected OutputSlot.");


    IOutputSlot* input = GetInputSlot(0).GetConnection();
    const TensorShape& inputShape = input->GetTensorInfo().GetShape();
    const TensorShape filterShape = m_Weight->GetTensorInfo().GetShape();

    // If we support multiple batch dimensions in the future, then this assert will need to change.
    BOOST_ASSERT_MSG(inputShape.GetNumDimensions() == 4, "Convolutions will always have 4D input.");

    unsigned int inWidth = inputShape[3];
    unsigned int inHeight = inputShape[2];
    unsigned int inBatchSize = inputShape[0];

    unsigned int filterWidth = filterShape[3];
    unsigned int readWidth = (inWidth + m_Param.m_PadLeft + m_Param.m_PadRight) - (filterWidth);
    unsigned int outWidth =  1+(readWidth / m_Param.m_StrideX);

    unsigned int filterHeight = filterShape[2];
    unsigned int readHeight = (inHeight + m_Param.m_PadTop + m_Param.m_PadBottom) - (filterHeight);
    unsigned int outHeight = 1+(readHeight / m_Param.m_StrideY);

    unsigned int outChannels = filterShape[0];
    unsigned int outBatchSize = inBatchSize;

    TensorShape shapeOut({outBatchSize, outChannels, outHeight, outWidth});
    ConditionalThrowIfNotEqual<LayerValidationException>(
        "Convolution2dLayer: TensorShape set on OutputSlot[0] does not match the inferred shape.",
        GetOutputSlot(0).GetTensorInfo().GetShape(),
        shapeOut);
}

} // namespace armnn
