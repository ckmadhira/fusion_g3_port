/*
 * Copyright (c) Qualcomm Innovation Center, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include <executorch/backends/qualcomm/aot/wrappers/QuantizeParamsWrapper.h>
#include <executorch/backends/qualcomm/runtime/Logging.h>
#include <executorch/runtime/core/error.h>

#include <memory>
#include <string>

#include "QnnTypes.h"

#define QNN_TENSOR_VER_PTR(x) (&((x).v2))
#define QNN_OP_VER_PTR(x) (&((x).v1))

namespace executorch {
namespace backends {
namespace qnn {
class TensorWrapper {
 public:
  explicit TensorWrapper(
      const std::string& tensor_name,
      Qnn_TensorType_t tensor_type,
      Qnn_DataType_t data_type,
      std::unique_ptr<QuantizeParamsWrapper> quantize_params,
      std::uint32_t rank,
      const std::uint32_t dims[],
      const std::uint8_t dynamic_dims[],
      std::uint32_t bytes,
      const void* data = nullptr,
      bool copy_data = false);

  executorch::runtime::Error FillDataBuffer(
      const void* data,
      bool copy_data = false);

  executorch::runtime::Error AllocateDataBuffer();

  // update qnn tensor meta
  // this function is used to recover metadata from QNN context binary.
  void UpdateQnnTensorMeta(const Qnn_Tensor_t& tensor_src);

  Qnn_Tensor_t CloneTensorStruct() const {
    return tensor_;
  };

  // Return true if the tensor_handle_ is not null, and has been created:
  bool IsTensorCreated() const {
    return created_;
  };

  void SetTensorCreated() {
    created_ = true;
  }

  // Return true if the tensor is static:
  bool IsTensorStatic() const {
    return QNN_TENSOR_VER_PTR(tensor_)->type == QNN_TENSOR_TYPE_STATIC;
  };

  std::uint32_t* GetDims() const {
    return QNN_TENSOR_VER_PTR(tensor_)->dimensions;
  };

  void SetDims(const int32_t* dims, size_t rank) {
    if (rank != QNN_TENSOR_VER_PTR(tensor_)->rank) {
      QNN_EXECUTORCH_LOG_WARN(
          "Tensor rank mismatches: %u vs %u, ignore current operation",
          QNN_TENSOR_VER_PTR(tensor_)->rank,
          rank);
      return;
    }
    for (int i = 0; i < rank; ++i) {
      QNN_TENSOR_VER_PTR(tensor_)->dimensions[i] = dims[i];
    }
  }

  Qnn_DataType_t GetDataType() const {
    return QNN_TENSOR_VER_PTR(tensor_)->dataType;
  };

  Qnn_MemHandle_t const GetMemHandle() {
    return QNN_TENSOR_VER_PTR(tensor_)->memHandle;
  };

  Qnn_TensorMemType_t GetMemType() const {
    return QNN_TENSOR_VER_PTR(tensor_)->memType;
  };

  Qnn_QuantizeParams_t GetQuantizeParams() const {
    return QNN_TENSOR_VER_PTR(tensor_)->quantizeParams;
  }

  const std::string& GetName() const {
    return qnn_tensor_name_;
  };

  std::uint32_t GetRank() const {
    return QNN_TENSOR_VER_PTR(tensor_)->rank;
  };

  std::uint32_t GetBytes() const {
    return bytes_;
  };

  const void* GetStaticTensorData() const {
    return QNN_TENSOR_VER_PTR(tensor_)->clientBuf.data;
  };

  executorch::runtime::Error SetName(const std::string& name);

  executorch::runtime::Error SetMemHandle(Qnn_MemHandle_t mem_handle);

  // executorch::runtime::Error SetDimensions(const uint32_t*);

 private:
  // need this to handle QNN_TENSOR_ERROR_NAME_HASH_COLLISION
  std::string qnn_tensor_name_;
  std::unique_ptr<QuantizeParamsWrapper> quantize_param_wrapper_;
  std::vector<std::uint32_t> dims_;
  std::vector<std::uint8_t> dynamic_dims_;
  std::uint32_t bytes_{0};
  std::unique_ptr<char[]> owned_data_;
  bool created_{false};

  Qnn_Tensor_t tensor_ = {
      .version = QNN_TENSOR_VERSION_2,
      .v2 = QNN_TENSOR_V2_INIT};
};
// base function for Create TensorWrapper
std::shared_ptr<TensorWrapper> CreateTensorWrapper(
    const std::string& tensor_name,
    Qnn_TensorType_t tensor_type,
    Qnn_DataType_t data_type,
    std::unique_ptr<QuantizeParamsWrapper> quantize_param_wrapper,
    std::uint32_t rank,
    const std::uint32_t dims[],
    const std::uint8_t dynamic_dims[],
    std::uint32_t bytes = 0,
    const void* data = nullptr,
    bool copy_data = false);

// Factory function to create TensorWrapper
std::shared_ptr<TensorWrapper> CreateTensorWrapper(
    Qnn_TensorType_t tensor_type,
    Qnn_DataType_t data_type,
    std::unique_ptr<QuantizeParamsWrapper> quantize_param_wrapper,
    std::uint32_t rank,
    const std::uint32_t dims[],
    const std::uint8_t dynamic_dims[],
    std::uint32_t bytes,
    const void* data = nullptr,
    bool copy_data = false);

std::shared_ptr<TensorWrapper> CreateTensorWrapper(const Qnn_Tensor_t& tensor);

// Utility to get size in bytes of QNN data type
std::uint32_t GetDataTypeSize(Qnn_DataType_t data_type);
} // namespace qnn
} // namespace backends
} // namespace executorch
