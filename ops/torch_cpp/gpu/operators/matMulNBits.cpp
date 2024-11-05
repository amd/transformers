/* Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved. */
#include "operators/matMulNBits.h"

#include "context.h"
#include <fstream>
#include <iostream>
#include <sstream>

namespace ryzenai::onnx_utils {

// =====================================================================================================================
// Constructs an DirectML-based MatMulNBitsOperator which evaluates a Gemm
// operation with the given parameters.
MatMulNBitsOperator::MatMulNBitsOperator(
    const Context &pContext,
    bool disableMetacmds, // If metacommands should be disabled for this
                          // operator.
    const MatMulNBitsParams &params)
    : m_gemm(params), m_dataType(StringToDataType(params.dataType)),
      m_quantDataType(StringToDataType(params.quantizedB)) {
  SharedInit();

  // Now convert them to the DirectML representation.
  DmlTensorDesc dmlTensorA = {};
  DmlTensorDesc dmlTensorB = {};
  DmlTensorDesc dmlTensorY = {};
  DmlTensorDesc dmlTensorC = {};

  DmlTensorDesc dmlTensorQBS = {};
  DmlTensorDesc dmlTensorQBZ = {};

  ConvertTensorDesc(*m_inputTensorDescVec[0], &dmlTensorA);
  ConvertTensorDesc(*m_inputTensorDescVec[1], &dmlTensorB);
  ConvertTensorDesc(*m_inputTensorDescVec[2], &dmlTensorQBS);
  if (m_gemm.hasZeroPoint) {
    ConvertTensorDesc(*m_inputTensorDescVec[3], &dmlTensorQBZ);
  }
  ConvertTensorDesc(*m_outputTensorDescVec[0], &dmlTensorY);

  if (params.hasC) {
    ConvertTensorDesc(*m_inputTensorDescVec[4], &dmlTensorC);
  }

  DmlTensorDesc dmlTensorQBDequant = {};
  TensorDesc dequantBTensor = *m_inputTensorDescVec[1].get();
  dequantBTensor.dataType = m_inputTensorDescVec[0].get()->dataType;
  dequantBTensor.name = L"dequantB";

  // dequantBTensor.totalBytes will be equal to size of dmlTensorB.totalBytes,
  // which for the case of int4/uint4 will be equal to XX = #row * #col *
  // sizeof(int8)/2. Now if we are dequantizing into a Float32, then output size
  // should be = XX * 2 * sizeof(float32). For case of float16, it should be XX
  // * 2 * sizeof(float16)

  if (dequantBTensor.dataType == DML_TENSOR_DATA_TYPE_FLOAT32) {
    dequantBTensor.totalBytes *= 2 * sizeof(float); // 4 bits to 32 bit
  } else if (dequantBTensor.dataType == DML_TENSOR_DATA_TYPE_FLOAT16) {
    dequantBTensor.totalBytes *= 2 * sizeof(uint16_t); // 4 bits to 16 bit

  } else {
    // bad things happen here
    assert("dequantBTensor.dataType is not Float or Float16");
  }

  ConvertTensorDesc(dequantBTensor, &dmlTensorQBDequant);

  std::vector<DML_TENSOR_DESC> quantizationParametersTensors;
  quantizationParametersTensors.push_back(dmlTensorQBS.desc);

  if (m_gemm.hasZeroPoint) {
    quantizationParametersTensors.push_back(dmlTensorQBZ.desc);
  }

  DML_DEQUANTIZE_OPERATOR_DESC dequantizeDesc = {};
  dequantizeDesc.InputTensor = &dmlTensorB.desc;
  dequantizeDesc.QuantizationType = m_gemm.hasZeroPoint
                                        ? DML_QUANTIZATION_TYPE_SCALE_ZERO_POINT
                                        : DML_QUANTIZATION_TYPE_SCALE;
  dequantizeDesc.QuantizationTensorCount =
      static_cast<uint32_t>(quantizationParametersTensors.size());
  dequantizeDesc.QuantizationTensors = quantizationParametersTensors.data();
  dequantizeDesc.OutputTensor = &dmlTensorQBDequant.desc;
  DML_OPERATOR_DESC dequantizeOpDesc = {DML_OPERATOR_DEQUANTIZE,
                                        &dequantizeDesc};

  DML_GEMM_OPERATOR_DESC gemmDesc = {};
  gemmDesc.ATensor = &dmlTensorA.desc;
  gemmDesc.BTensor = &dmlTensorQBDequant.desc;
  gemmDesc.CTensor = params.hasC ? &dmlTensorC.desc : nullptr;
  gemmDesc.OutputTensor = &dmlTensorY.desc;
  gemmDesc.TransA = DML_MATRIX_TRANSFORM_NONE;
  gemmDesc.TransB = DML_MATRIX_TRANSFORM_TRANSPOSE;
  gemmDesc.Alpha = 1.0f;
  gemmDesc.Beta = params.hasC ? 1.0f : 0.0f;
  DML_OPERATOR_DESC gemmOpDesc = {DML_OPERATOR_GEMM, &gemmDesc};

  // Construct the graph
  std::vector<DML_INPUT_GRAPH_EDGE_DESC> inputEdges;
  std::vector<DML_INTERMEDIATE_GRAPH_EDGE_DESC> intermediateEdges;
  std::vector<DML_OUTPUT_GRAPH_EDGE_DESC> outputEdges;

  std::vector<const DML_OPERATOR_DESC *> opDescs = {
      &dequantizeOpDesc,
      &gemmOpDesc,
  };

  DML_INPUT_GRAPH_EDGE_DESC secondInputToDequantizeEdge = {};
  secondInputToDequantizeEdge.GraphInputIndex = 1;
  secondInputToDequantizeEdge.ToNodeIndex = 0;
  secondInputToDequantizeEdge.ToNodeInputIndex = 0;
  inputEdges.push_back(secondInputToDequantizeEdge);

  DML_INPUT_GRAPH_EDGE_DESC scaleToDequantizeEdge = {};
  scaleToDequantizeEdge.GraphInputIndex = 2;
  scaleToDequantizeEdge.ToNodeIndex = 0;
  scaleToDequantizeEdge.ToNodeInputIndex = 1;
  inputEdges.push_back(scaleToDequantizeEdge);

  if (m_gemm.hasZeroPoint) {
    DML_INPUT_GRAPH_EDGE_DESC zeroPointToDequantizeEdge = {};
    zeroPointToDequantizeEdge.GraphInputIndex = 3;
    zeroPointToDequantizeEdge.ToNodeIndex = 0;
    zeroPointToDequantizeEdge.ToNodeInputIndex = 2;
    inputEdges.push_back(zeroPointToDequantizeEdge);
  }

  DML_INPUT_GRAPH_EDGE_DESC firstInputToGemmEdge = {};
  firstInputToGemmEdge.GraphInputIndex = 0;
  firstInputToGemmEdge.ToNodeIndex = 1;
  firstInputToGemmEdge.ToNodeInputIndex = 0;
  inputEdges.push_back(firstInputToGemmEdge);

  if (m_gemm.hasC) {
    DML_INPUT_GRAPH_EDGE_DESC secondInputToGemmEdge = {};
    secondInputToGemmEdge.GraphInputIndex = 4;
    secondInputToGemmEdge.ToNodeIndex = 1;
    secondInputToGemmEdge.ToNodeInputIndex = 2;
    inputEdges.push_back(secondInputToGemmEdge);
  }

  DML_INTERMEDIATE_GRAPH_EDGE_DESC dequantizeToGemmEdge = {};
  dequantizeToGemmEdge.FromNodeIndex = 0;
  dequantizeToGemmEdge.FromNodeOutputIndex = 0;
  dequantizeToGemmEdge.ToNodeIndex = 1;
  dequantizeToGemmEdge.ToNodeInputIndex = 1;
  intermediateEdges.push_back(dequantizeToGemmEdge);

  DML_OUTPUT_GRAPH_EDGE_DESC gemmToOutputEdge = {};
  gemmToOutputEdge.FromNodeIndex = 1;
  gemmToOutputEdge.FromNodeOutputIndex = 0;
  gemmToOutputEdge.GraphOutputIndex = 0;
  outputEdges.push_back(gemmToOutputEdge);

  DML_GRAPH_DESC graphDesc = {};
  std::vector<DML_GRAPH_NODE_DESC> dmlGraphNodes(
      static_cast<uint32_t>(opDescs.size()));
  std::vector<CComPtr<IDMLOperator>> dmlOperators(
      static_cast<uint32_t>(opDescs.size()));
  std::vector<DML_OPERATOR_GRAPH_NODE_DESC> dmlOperatorGraphNodes(
      static_cast<uint32_t>(opDescs.size()));
  std::vector<DML_GRAPH_EDGE_DESC> dmlInputEdges(
      static_cast<uint32_t>(inputEdges.size()));
  std::vector<DML_GRAPH_EDGE_DESC> dmlOutputEdges(
      static_cast<uint32_t>(outputEdges.size()));
  std::vector<DML_GRAPH_EDGE_DESC> dmlIntermediateEdges(
      static_cast<uint32_t>(intermediateEdges.size()));

  // build the graph description from the above edges
  graphDesc.InputCount = static_cast<uint32_t>(inputEdges.size());
  graphDesc.OutputCount = static_cast<uint32_t>(outputEdges.size());
  graphDesc.NodeCount = static_cast<uint32_t>(opDescs.size());
  HRESULT status = S_OK;

  for (size_t i = 0; i < graphDesc.NodeCount; ++i) {
    // Create the operator.
    status = pContext.DmlDevice()->CreateOperator(
        opDescs[i], IID_PPV_ARGS(&dmlOperators[i]));
    dmlOperatorGraphNodes[i] = DML_OPERATOR_GRAPH_NODE_DESC{dmlOperators[i]};
    dmlGraphNodes[i] = DML_GRAPH_NODE_DESC{DML_GRAPH_NODE_TYPE_OPERATOR,
                                           &dmlOperatorGraphNodes[i]};
    if (FAILED(status)) {
      throw std::runtime_error("Failed to compile a DirectML operator.");
    }
  }

  graphDesc.Nodes = dmlGraphNodes.data();

  // set the input edges
  graphDesc.InputEdgeCount = static_cast<uint32_t>(inputEdges.size());
  for (size_t i = 0; i < graphDesc.InputEdgeCount; ++i) {
    dmlInputEdges[i] =
        DML_GRAPH_EDGE_DESC{DML_GRAPH_EDGE_TYPE_INPUT, &inputEdges[i]};
  }
  graphDesc.InputEdges = dmlInputEdges.data();

  // set the output edges
  graphDesc.OutputEdgeCount = graphDesc.OutputCount;
  for (size_t i = 0; i < graphDesc.OutputEdgeCount; ++i) {
    dmlOutputEdges[i] =
        DML_GRAPH_EDGE_DESC{DML_GRAPH_EDGE_TYPE_OUTPUT, &outputEdges[i]};
  }
  graphDesc.OutputEdges = dmlOutputEdges.data();

  // set the intermediate edges
  graphDesc.IntermediateEdgeCount =
      static_cast<uint32_t>(intermediateEdges.size());
  for (size_t i = 0; i < graphDesc.IntermediateEdgeCount; ++i) {
    dmlIntermediateEdges[i] = DML_GRAPH_EDGE_DESC{
        DML_GRAPH_EDGE_TYPE_INTERMEDIATE, &intermediateEdges[i]};
  }
  graphDesc.IntermediateEdges = dmlIntermediateEdges.data();

  CComPtr<IDMLDevice1> dmlDevice1;
  status = pContext.DmlDevice()->QueryInterface(IID_PPV_ARGS(&dmlDevice1));
  if (FAILED(status)) {
    throw std::runtime_error("Failed to acquire dml graph interface.");
  }

  DML_EXECUTION_FLAGS executionFlags = DML_EXECUTION_FLAG_NONE;
  if (disableMetacmds) {
    executionFlags = DML_EXECUTION_FLAG_DISABLE_META_COMMANDS;
  }
  executionFlags |= DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION;

  status = dmlDevice1->CompileGraph(&graphDesc, executionFlags,
                                    IID_PPV_ARGS(&m_operator));
  if (FAILED(status)) {
    throw std::runtime_error("Failed to compile a DirectML operator.");
  }
}

// =====================================================================================================================
// Code shared between our constructors.
void MatMulNBitsOperator::SharedInit() {
  if (!(m_dataType == DML_TENSOR_DATA_TYPE_FLOAT16 ||
        m_dataType == DML_TENSOR_DATA_TYPE_FLOAT32)) {
    std::wstringstream ss;
    ss << "MatMulNBits only supports Float16, not " << m_gemm.dataType.c_str();
    throw std::runtime_error(winrt::to_string(ss.str()));
  }

  if ((m_gemm.quantizedB != L"Uint4") && (m_gemm.quantizedB != L"Int4")) {
    std::wstringstream ss;
    ss << "MatMulNBits must have int4/uint4 quantized b matrix";
    throw std::runtime_error(winrt::to_string(ss.str()));
  }

  if (m_gemm.transB == false) {
    std::wstringstream ss;
    ss << "MatMulNBits only supports transposed B matrix";
    throw std::runtime_error(winrt::to_string(ss.str()));
  }

  if (m_gemm.quantizedA != L"None") {
    std::wstringstream ss;
    ss << "MatMulNBits does not support quantized A matrix";
    throw std::runtime_error(winrt::to_string(ss.str()));
  }

  // Build the tensor descriptors. For DirectML's benefit, we must have one
  // pointer for every possible tensor and must use empty pointers as
  // placeholders for unused optional tensors.
  const int64 heightA = m_gemm.transA ? m_gemm.k : m_gemm.m;
  const int64 widthA = m_gemm.transA ? m_gemm.m : m_gemm.k;
  const int64 heightB = m_gemm.transB ? m_gemm.n : m_gemm.k;
  const int64 widthB = m_gemm.transB ? m_gemm.k : m_gemm.n;

  const std::vector<int64> sizeA{m_gemm.batches, heightA, widthA};
  const std::vector<int64> sizeB{m_gemm.batches, heightB, widthB};
  const std::vector<int64> sizeY{m_gemm.batches, m_gemm.m, m_gemm.n};

  const std::vector<int64> sizeQBS{m_gemm.batches, heightB,
                                   widthB / m_gemm.quantizedBlockB};
  const std::vector<int64> sizeQBZ{m_gemm.batches, heightB,
                                   widthB / m_gemm.quantizedBlockB};

  const std::vector<int64> stridesQBS{-1, -1, -1};
  const std::vector<int64> stridesQBZ{-1, -1, -1};

  const std::vector<int64> stridesA{m_gemm.stridesA[0], m_gemm.stridesA[1],
                                    m_gemm.stridesA[2]};
  const std::vector<int64> stridesB{m_gemm.stridesB[0], m_gemm.stridesB[1],
                                    m_gemm.stridesB[2]};
  const std::vector<int64> stridesY{m_gemm.stridesY[0], m_gemm.stridesY[1],
                                    m_gemm.stridesY[2]};

  m_inputTensorDescVec.emplace_back(CreateTensorDesc(
      L"ATensorData", m_gemm.orderA, m_dataType, sizeA, stridesA));
  m_inputTensorDescVec.emplace_back(CreateTensorDesc(
      L"BTensorData", m_gemm.orderB, m_quantDataType, sizeB, stridesB));
  m_inputTensorDescVec.emplace_back(CreateTensorDesc(
      L"BTensorScale", m_gemm.orderB, m_dataType, sizeQBS, stridesQBS));
  if (m_gemm.hasZeroPoint) {
    // if zero point is used then its type is same as BTensorData, i.e
    // uint4/int4
    m_inputTensorDescVec.emplace_back(
        CreateTensorDesc(L"BTensorZeroPoint", m_gemm.orderB, m_quantDataType,
                         sizeQBZ, stridesQBZ));
  }

  m_outputTensorDescVec.emplace_back(
      CreateTensorDesc(L"Y", m_gemm.orderY, m_dataType, sizeY, stridesY));
  if (m_gemm.hasC) {
    // Note that C always has the same size as Y.
    m_inputTensorDescVec.emplace_back(CreateTensorDesc(
        L"CTensorData", m_gemm.orderY, m_dataType, sizeY, stridesY));
  }
}

// =====================================================================================================================
// helper to parse out tensor binary paths if used
hstring loadPath(const hstring &name, MatMulNBitsParams params) {
  hstring loadPath = {};
  if (name == L"ATensorData") {
    loadPath = params.ATensor;
  } else if (name == L"BTensorData") {
    loadPath = params.BTensor;
  } else if (name == L"BTensorScale") {
    loadPath = params.BTensorScale;
  } else if (name == L"BTensorZeroPoint") {
    loadPath = params.BTensorZeroPoint;
  } else if (name == L"CTensorData") {
    loadPath = params.CTensor;
  }

  return loadPath;
}

// =====================================================================================================================
// loads tensor binary data from disk
void MatMulNBitsOperator::LoadInputBuffers(TensorVector *inputBuffer) const {
  for (auto it = inputBuffer->begin(); it != inputBuffer->end(); ++it) {
    auto *tensor = it->get();
    if (tensor != nullptr) {
      const auto name = tensor->Desc().name;
      const auto path = loadPath(name, m_gemm);
      if (path.empty() == false) {
        std::ifstream file(winrt::to_string(path),
                           std::ios::binary | std::ios::ate);
        // check successful load
        if (!file.is_open()) {
          throw std::runtime_error(
              "MatMulNBitsOperator::LoadInputBuffers: failed to open file at "
              "path:" +
              winrt::to_string(path));
        }
        const std::size_t byteSize = file.tellg();
        file.seekg(0);
        // check size of tensor
        if (byteSize != tensor->Desc().totalBytes) {
          throw std::runtime_error(
              "MatMulNBitsOperator::LoadInputBuffers: tensor size mismatch.");
        }
        file.read(reinterpret_cast<char *>(tensor->Buffer()), byteSize);
      }
    }
  }
};

} // namespace ryzenai::onnx_utils
