//
// Copyright © 2020 Arm Ltd and Contributors. All rights reserved.
// SPDX-License-Identifier: MIT
//

#pragma once

#include <armnn/utility/IgnoreUnused.hpp>

#include "DelegateUtils.hpp"

#include <tensorflow/lite/builtin_ops.h>
#include <tensorflow/lite/c/builtin_op_data.h>
#include <tensorflow/lite/c/common.h>
#include <tensorflow/lite/minimal_logging.h>
#include <numeric>

namespace armnnDelegate
{

TfLiteStatus CreateOutputTensorShape(const armnn::TensorInfo& inputTensorInfo,
                                           const std::vector<int32_t>& targetShape,
                                           armnn::ReshapeDescriptor& reshapeDesc)
{
    std::vector<unsigned int> outputDims(targetShape.begin(), targetShape.end());
    const auto stretchDim = std::find(targetShape.begin(), targetShape.end(), -1);

    if (stretchDim != targetShape.end())
    {
        if (std::find(std::next(stretchDim), targetShape.end(), -1) != targetShape.end())
        {
            // Return kTfLiteError and log the error after returning
            return kTfLiteError;
        }

        auto targetNumElements =
            armnn::numeric_cast<unsigned int>(
                std::accumulate(targetShape.begin(), targetShape.end(), -1, std::multiplies<int32_t>()));

        auto stretchIndex = static_cast<size_t>(std::distance(targetShape.begin(), stretchDim));
        outputDims[stretchIndex] = inputTensorInfo.GetNumElements() / targetNumElements;
    }

    armnn::TensorShape outputShape = armnn::TensorShape(static_cast<unsigned int>(outputDims.size()),
                                                        outputDims.data());
    reshapeDesc.m_TargetShape = outputShape;
    return kTfLiteOk;
}

TfLiteStatus VisitReshapeOperator(DelegateData& delegateData,
                                  TfLiteContext* tfLiteContext,
                                  TfLiteNode* tfLiteNode,
                                  int nodeIndex,
                                  int32_t operatorCode)
{
    auto numInputs = tfLiteNode->inputs->size;

    if (numInputs == 2)
    {
        TF_LITE_ENSURE_STATUS(ValidateNumInputs(tfLiteContext, tfLiteNode, 2, nodeIndex));
    }
    else
    {
        TF_LITE_ENSURE_STATUS(ValidateNumInputs(tfLiteContext, tfLiteNode, 1, nodeIndex));
    }
    TF_LITE_ENSURE_STATUS(ValidateNumOutputs(tfLiteContext, tfLiteNode, 1, nodeIndex));

    const TfLiteTensor* tfLiteTensors = tfLiteContext->tensors;
    const TfLiteTensor& tfLiteInputTensor0 = tfLiteTensors[tfLiteNode->inputs->data[0]];
    if (IsDynamicTensor(tfLiteInputTensor0))
    {
        TF_LITE_MAYBE_KERNEL_LOG(tfLiteContext,
                                 "TfLiteArmnnDelegate: Dynamic input tensors are not supported in "
                                 "operator #%d node #%d: ",
                                 operatorCode, nodeIndex);
        return kTfLiteError;
    }

    const TfLiteTensor& tfLiteOutputTensor = tfLiteTensors[tfLiteNode->outputs->data[0]];
    if (IsDynamicTensor(tfLiteOutputTensor))
    {
        TF_LITE_MAYBE_KERNEL_LOG(tfLiteContext,
                                 "TfLiteArmnnDelegate: Dynamic output tensors are not supported in "
                                 "operator #%d node #%d: ",
                                 operatorCode, nodeIndex);
        return kTfLiteError;
    }

    const armnn::TensorInfo& inputTensorInfo0 = GetTensorInfoForTfLiteTensor(tfLiteInputTensor0);
    const armnn::TensorInfo& outputTensorInfo = GetTensorInfoForTfLiteTensor(tfLiteOutputTensor);

    armnn::ReshapeDescriptor reshapeDesc;

    // The new shape can be defined by either a second input tensor or by a builtin option, we need to check for both.
    if (numInputs == 2)
    {
        const TfLiteTensor& tfLiteShapeInputTensor = tfLiteTensors[tfLiteNode->inputs->data[1]];
        if (IsDynamicTensor(tfLiteShapeInputTensor))
        {
            TF_LITE_MAYBE_KERNEL_LOG(tfLiteContext,
                                     "TfLiteArmnnDelegate: Dynamic input tensors are not supported in "
                                     "operator #%d node #%d: ",
                                     operatorCode, nodeIndex);
            return kTfLiteError;
        }

        // Get the shape data out of the input tensor
        std::vector<int32_t> targetShape;
        auto* shapeTensorDataPtr = tflite::GetTensorData<int32_t>(&tfLiteShapeInputTensor);
        auto shapeTensorNumValues = tfLiteShapeInputTensor.dims->data[0];
        for (auto i=0; i < shapeTensorNumValues; ++i)
        {
            targetShape.push_back(*(shapeTensorDataPtr+i));
        }

        // Use the data to create the required tensor shape.
        if (CreateOutputTensorShape(inputTensorInfo0, targetShape, reshapeDesc) != kTfLiteOk)
        {
            TF_LITE_MAYBE_KERNEL_LOG(tfLiteContext,
                                     "TfLiteArmnnDelegate: At most one component of shape can be -1 in: "
                                     "operator #%d node #%d: ",
                                     operatorCode, nodeIndex);
            return kTfLiteError;
        }
    }
    else if (tfLiteNode->builtin_data)
    {
        std::vector<int32_t> targetShape;
        TfLiteReshapeParams* reshapeOptions =
                    reinterpret_cast<TfLiteReshapeParams*>(tfLiteNode->builtin_data);
        for (int i=0; i < reshapeOptions->num_dimensions; ++i)
        {
            targetShape.push_back(reshapeOptions->shape[i]);
        }
        if (CreateOutputTensorShape(inputTensorInfo0, targetShape, reshapeDesc) != kTfLiteOk)
        {
            TF_LITE_MAYBE_KERNEL_LOG(tfLiteContext,
                                     "TfLiteArmnnDelegate: At most one component of shape can be -1 in: "
                                     "operator #%d node #%d: ",
                                     operatorCode, nodeIndex);
            return kTfLiteError;
        }
    }
    else
    {
        TF_LITE_MAYBE_KERNEL_LOG(tfLiteContext,
                                 "Target shape not defined in reshape parameters or input tensor. "
                                 "At least one method required in operator #%d node #%d: ",
                                 operatorCode, nodeIndex);
    }

    bool isSupported = false;
    auto validateFunc = [&](const armnn::TensorInfo& outInfo, bool& isSupported)
    {
        FORWARD_LAYER_SUPPORT_FUNC(__func__,
                                   tfLiteContext,
                                   IsReshapeSupported,
                                   delegateData.m_Backends,
                                   isSupported,
                                   inputTensorInfo0,
                                   outInfo,
                                   reshapeDesc);
    };

    if (!delegateData.m_Network)
    {
        validateFunc(outputTensorInfo, isSupported);
        return isSupported ? kTfLiteOk : kTfLiteError;
    }

    armnn::IConnectableLayer* layer = delegateData.m_Network->AddReshapeLayer(reshapeDesc);
    ARMNN_ASSERT(layer != nullptr);

    armnn::IOutputSlot& outputSlot = layer->GetOutputSlot(0);
    outputSlot.SetTensorInfo(outputTensorInfo);

    // Connect
    return Connect(layer, tfLiteNode, delegateData);
}

TfLiteStatus VisitSqueezeOperator(DelegateData& delegateData,
                                  TfLiteContext* tfLiteContext,
                                  TfLiteNode* tfLiteNode,
                                  int nodeIndex,
                                  int32_t operatorCode)
{
    armnn::IgnoreUnused(delegateData,
                        tfLiteContext,
                        tfLiteNode,
                        nodeIndex,
                        operatorCode);

    return kTfLiteError;
}

TfLiteStatus VisitExpandDimsOperator(DelegateData& delegateData,
                                     TfLiteContext* tfLiteContext,
                                     TfLiteNode* tfLiteNode,
                                     int nodeIndex,
                                     int32_t operatorCode)
{
    armnn::IgnoreUnused(delegateData,
                        tfLiteContext,
                        tfLiteNode,
                        nodeIndex,
                        operatorCode);

    return kTfLiteError;
}

} // namespace armnnDelegate
