/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/runtime/kernel/opencl/kernel/arithmetic.h"
#include <set>
#include <vector>
#include <string>
#include "schema/model_generated.h"
#include "src/kernel_registry.h"
#include "src/runtime/kernel/opencl/utils.h"
#ifndef PROGRAM_WITH_IL
#include "src/runtime/kernel/opencl/cl/arithmetic.cl.inc"
#endif

using mindspore::kernel::KERNEL_ARCH::kGPU;
using mindspore::lite::KernelRegistrar;

namespace mindspore::kernel {

ArithmeticOpenCLKernel::~ArithmeticOpenCLKernel() {
  if (weight_ptr_ != nullptr) {
    auto allocator = runtime_->GetAllocator();
    allocator->Free(weight_ptr_);
    weight_ptr_ = nullptr;
  }
}

std::vector<size_t> ArithmeticOpenCLKernel::InitGlobalSize() const {
  const size_t global_x = out_tensors_[0]->Width();
  const size_t global_y = out_tensors_[0]->Height();
  const size_t global_z = UP_ROUND_DIV(out_tensors_[0]->Channel(), 4);
  std::vector<size_t> global = {global_x, global_y, global_z};
  return global;
}

void ArithmeticOpenCLKernel::Image2dGetWorkGroupSize() {
  local_size_ = {16, 16};
  if (out_tensors_[0]->GetFormat() == schema::Format_NHWC4) {
    size_t H = out_tensors_[0]->Batch() * out_tensors_[0]->Height();
    size_t W = out_tensors_[0]->Width() * UP_DIV(out_tensors_[0]->Channel(), C4NUM);
    global_size_ = {W, H};
  } else if (out_tensors_[0]->GetFormat() == schema::Format_NC4) {
    size_t H = out_tensors_[0]->Batch();
    size_t W = UP_DIV(out_tensors_[0]->Channel(), C4NUM);
    global_size_ = {W, H};
  } else {
    MS_LOG(ERROR) << "Unspport data format " << out_tensors_[0]->GetFormat();
  }
}

void ArithmeticOpenCLKernel::BufferGetWorkGroupSize() {
  uint32_t element_num = out_tensors_[0]->ElementsC4Num();
  global_size_ = {element_num};
}

int ArithmeticOpenCLKernel::GetImageSize(size_t idx, std::vector<size_t> *img_size) {
  size_t im_dst_x, im_dst_y;
  if (out_tensors_[0]->GetFormat() == schema::Format_NHWC4) {
    im_dst_x = out_tensors_[0]->Width() * UP_DIV(out_tensors_[0]->Channel(), C4NUM);
    im_dst_y = out_tensors_[0]->Batch() * out_tensors_[0]->Height();
  } else if (out_tensors_[0]->GetFormat() == schema::Format_NC4) {
    im_dst_y = out_tensors_[0]->Batch();
    im_dst_x = UP_DIV(out_tensors_[0]->Channel(), C4NUM);
  } else {
    MS_LOG(ERROR) << "Unspport data format " << out_tensors_[0]->GetFormat();
    return RET_ERROR;
  }
#ifdef ENABLE_FP16
  size_t img_dtype = CL_HALF_FLOAT;
#else
  size_t img_dtype = CL_FLOAT;
#endif
  img_size->clear();
  std::vector<size_t> vec{im_dst_x, im_dst_y, img_dtype};
  *img_size = vec;
  return RET_OK;
}

int ArithmeticOpenCLKernel::InitBuffer() {
  const ArithmeticParameter *arithmetic_parameter = reinterpret_cast<const ArithmeticParameter *>(op_parameter_);
  if (!arithmetic_parameter->broadcasting_) {
    if (in_tensors_[1]->TensorType() == schema::NodeType_ValueNode && in_tensors_[1]->Data() != nullptr) {
      auto allocatdor = runtime_->GetAllocator();
      std::vector<size_t> img_size;
      GetImageSize(0, &img_size);
      weight_ptr_ = allocatdor->CreateImageFromHost(in_tensors_[1]->Data(), in_tensors_[1]->ElementsNum(), img_size);
      return RET_OK;
    }
  }
  return RET_OK;
}
int ArithmeticOpenCLKernel::Init() {
  runtime_ = lite::opencl::OpenCLRuntime::GetInstance();
  std::string kernel_name;

  const ArithmeticParameter *arithmetic_parameter = reinterpret_cast<const ArithmeticParameter *>(op_parameter_);
  if (arithmetic_parameter->broadcasting_ && in_tensors_[1]->TensorType() == schema::NodeType_ValueNode &&
      in_tensors_[1]->Data() != nullptr) {
    element_flag_ = false;
    kernel_name = "BoardcastArith";
  } else {
    element_flag_ = true;
    switch (op_parameter_->type_) {
      case PrimitiveType_Mul:
        kernel_name = "ElementMul";
        break;
      case PrimitiveType_Add:
        kernel_name = "ElementAdd";
        break;
      case PrimitiveType_Sub:
        kernel_name = "ElementSub";
        break;
      case PrimitiveType_Div:
        kernel_name = "ElementDiv";
        break;
      default:
        MS_LOG(ERROR) << "Error Operator type " << op_parameter_->type_;
        break;
    }
  }

  lite::STATUS error_code = RET_OK;
#ifdef PROGRAM_WITH_IL
  kernel_ = runtime_->GetKernelFromBinary(kernel_name);
#else
  if (out_mem_type_ == OpenCLMemType::IMG) {
    kernel_name += "_IMG";
  } else {
    kernel_name += "_BUF";
  }
  std::string program_name = "Arithmetic";
  std::set<std::string> build_options;
  std::string source = arithmetic_source;
  runtime_->LoadSource(program_name, source);
  error_code = runtime_->BuildKernel(kernel_, program_name, kernel_name, build_options);
#endif
  if (error_code != RET_OK) {
    return error_code;
  }

  auto format = schema::Format_NHWC4;
  if (arithmetic_parameter->ndim_ == 2) {
    format = schema::Format_NC4;
  }
  in_ori_format_ = in_tensors_[0]->GetFormat();
  out_ori_format_ = out_tensors_[0]->GetFormat();
  in_tensors_[0]->SetFormat(format);
  if (element_flag_ && in_tensors_[1]->TensorType() != schema::NodeType_ValueNode) {
    in_tensors_[1]->SetFormat(format);
  }
  out_tensors_[0]->SetFormat(format);
  Image2dGetWorkGroupSize();
  InitBuffer();
  return RET_OK;
}

int ArithmeticOpenCLKernel::Run() {
  MS_LOG(DEBUG) << this->name() << " Running!";

  int arg_idx = 0;
  runtime_->SetKernelArg(kernel_, arg_idx++, in_tensors_[0]->Data());
  if (element_flag_) {
    void *weight = weight_ptr_ == nullptr ? in_tensors_[1]->Data() : weight_ptr_;
    runtime_->SetKernelArg(kernel_, arg_idx++, weight);
  } else {
    float value = static_cast<float *>(in_tensors_[1]->Data())[0];
    switch (op_parameter_->type_) {
      case PrimitiveType_Mul:
        weight_ = value;
        break;
      case PrimitiveType_Add:
        bias_ = value;
        break;
      case PrimitiveType_Sub:
        bias_ = -1 * value;
        break;
      case PrimitiveType_Div:
        weight_ = 1 / value;
        break;
      default:
        MS_LOG(ERROR) << "Error Operator type " << op_parameter_->type_;
        break;
    }
    runtime_->SetKernelArg(kernel_, arg_idx++, weight_);
    runtime_->SetKernelArg(kernel_, arg_idx++, bias_);
  }
  runtime_->SetKernelArg(kernel_, arg_idx++, out_tensors_[0]->Data());

  int H = 0;
  int W = 0;
  if (out_tensors_[0]->GetFormat() == schema::Format_NHWC4) {
    H = out_tensors_[0]->Batch() * out_tensors_[0]->Height();
    W = out_tensors_[0]->Width() * UP_DIV(out_tensors_[0]->Channel(), C4NUM);
  } else if (out_tensors_[0]->GetFormat() == schema::Format_NC4) {
    H = out_tensors_[0]->Batch();
    W = UP_DIV(out_tensors_[0]->Channel(), C4NUM);
  } else {
    MS_LOG(ERROR) << "Error output type " << out_tensors_[0]->GetFormat();
    return RET_ERROR;
  }
  cl_int2 output_shape{W, H};
  runtime_->SetKernelArg(kernel_, arg_idx++, output_shape);
  runtime_->RunKernel(kernel_, global_size_, local_size_, nullptr);
  return RET_OK;
}

kernel::LiteKernel *OpenCLBiasAddKernelCreator(const std::vector<lite::tensor::Tensor *> &inputs,
                                               const std::vector<lite::tensor::Tensor *> &outputs,
                                               OpParameter *opParameter, const lite::Context *ctx,
                                               const kernel::KernelKey &desc, const lite::PrimitiveC *primitive);

kernel::LiteKernel *OpenCLArithmeticKernelCreator(const std::vector<lite::tensor::Tensor *> &inputs,
                                                  const std::vector<lite::tensor::Tensor *> &outputs,
                                                  OpParameter *opParameter, const lite::Context *ctx,
                                                  const kernel::KernelKey &desc,
                                                  const mindspore::lite::PrimitiveC *primitive) {
  const ArithmeticParameter *arithmetic_parameter = reinterpret_cast<const ArithmeticParameter *>(opParameter);
  if (arithmetic_parameter->broadcasting_) {
    for (size_t i = 0; i < arithmetic_parameter->ndim_; i++) {
      if (arithmetic_parameter->in_shape1_[i] != 0 && arithmetic_parameter->in_shape1_[i] != 1) {
        return OpenCLBiasAddKernelCreator(inputs, outputs, opParameter, ctx, desc, primitive);
      }
    }
  }
  auto *kernel =
    new (std::nothrow) ArithmeticOpenCLKernel(reinterpret_cast<OpParameter *>(opParameter), inputs, outputs, ctx);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "Create OpenCL Arithmetic kernel failed!";
    return nullptr;
  }
  auto ret = kernel->Init();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Init kernel failed, name: Arithmetic";
    delete kernel;
    return nullptr;
  }
  return kernel;
}

REG_KERNEL(kGPU, kNumberTypeFloat32, PrimitiveType_Mul, OpenCLArithmeticKernelCreator)
REG_KERNEL(kGPU, kNumberTypeFloat32, PrimitiveType_Add, OpenCLArithmeticKernelCreator)
REG_KERNEL(kGPU, kNumberTypeFloat32, PrimitiveType_Sub, OpenCLArithmeticKernelCreator)
REG_KERNEL(kGPU, kNumberTypeFloat32, PrimitiveType_Div, OpenCLArithmeticKernelCreator)
}  // namespace mindspore::kernel
