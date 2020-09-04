/**
 * Copyright 2019 Huawei Technologies Co., Ltd
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

#include "runtime/device/gpu/kernel_info_setter.h"
#include <string>
#include <memory>
#include "backend/kernel_compiler/kernel.h"
#include "utils/utils.h"
#include "utils/ms_context.h"
#include "backend/kernel_compiler/gpu/gpu_kernel_factory.h"
#include "backend/kernel_compiler/kernel_build_info.h"
#include "backend/session/anf_runtime_algorithm.h"
#include "backend/kernel_compiler/common_utils.h"
#include "utils/ms_utils.h"
#include "backend/kernel_compiler/oplib/oplib.h"
#include "backend/kernel_compiler/oplib/opinfo.h"

namespace mindspore {
namespace device {
namespace gpu {
using AnfAlgo = mindspore::session::AnfRuntimeAlgorithm;
using mindspore::kernel::KernelBuildInfo;
namespace {
bool CheckKernelInfo(const std::shared_ptr<KernelBuildInfo> &alternative_kernel_info,
                     const std::shared_ptr<KernelBuildInfo> &selected_kernel_info) {
  MS_EXCEPTION_IF_NULL(selected_kernel_info);
  MS_EXCEPTION_IF_NULL(alternative_kernel_info);
  size_t selected_input_num = selected_kernel_info->GetInputNum();
  size_t alternative_input_num = alternative_kernel_info->GetInputNum();
  if (selected_input_num != alternative_input_num) {
    return false;
  }
  for (size_t i = 0; i < selected_input_num; i++) {
    if (selected_kernel_info->GetInputFormat(i) != alternative_kernel_info->GetInputFormat(i)) {
      return false;
    }
    if (selected_kernel_info->GetInputDeviceType(i) != alternative_kernel_info->GetInputDeviceType(i)) {
      return false;
    }
  }

  size_t selected_output_num = selected_kernel_info->GetOutputNum();
  size_t alternative_output_num = alternative_kernel_info->GetOutputNum();
  if (selected_output_num != alternative_output_num) {
    return false;
  }
  for (size_t i = 0; i < selected_output_num; i++) {
    if (selected_kernel_info->GetOutputFormat(i) != alternative_kernel_info->GetOutputFormat(i)) {
      return false;
    }
    if (selected_kernel_info->GetOutputDeviceType(i) != alternative_kernel_info->GetOutputDeviceType(i)) {
      return false;
    }
  }
  return true;
}

std::string SupportedTypeList(const CNodePtr &kernel_node) {
  std::string supported_type_lists =
    kernel::GpuKernelFactory::GetInstance().SupportedTypeList(AnfAlgo::GetCNodeName(kernel_node));
  if (!supported_type_lists.empty()) {
    return supported_type_lists;
  }
  std::vector<std::shared_ptr<KernelBuildInfo>> kernel_info_list;
  std::string op_name = AnfAlgo::GetCNodeName(kernel_node);
  auto op_info_ptr = mindspore::kernel::OpLib::FindOp(op_name, kernel::OpImplyType::kAKG);
  if (op_info_ptr == nullptr) {
    MS_LOG(EXCEPTION) << "Unsupported op [" << op_name << "]";
  }
  (void)ParseMetadata(kernel_node, op_info_ptr, kernel::Processor::CUDA, &kernel_info_list);
  for (size_t i = 0; i < kernel_info_list.size(); i++) {
    auto supported_akg_type = kernel_info_list[i]->GetAllInputDeviceTypes();
    auto supported_akg_type_out = kernel_info_list[i]->GetAllOutputDeviceTypes();
    std::string supported_akg_type_list = "in[";
    for (auto type : supported_akg_type) {
      supported_akg_type_list = supported_akg_type_list + mindspore::kernel::TypeId2String(type);
    }
    supported_type_lists = supported_type_lists + supported_akg_type_list + "], out[";
    supported_akg_type_list.clear();
    for (auto type : supported_akg_type_out) {
      supported_akg_type_list = supported_akg_type_list + mindspore::kernel::TypeId2String(type);
    }
    supported_type_lists = supported_type_lists + supported_akg_type_list + "]; ";
  }
  return supported_type_lists;
}

bool SelectAkgKernel(const CNodePtr &kernel_node, const std::shared_ptr<KernelBuildInfo> &selected_kernel_info) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  MS_EXCEPTION_IF_NULL(selected_kernel_info);
  std::vector<std::shared_ptr<KernelBuildInfo>> kernel_info_list;
  std::string op_name = AnfAlgo::GetCNodeName(kernel_node);

  auto op_info_ptr = mindspore::kernel::OpLib::FindOp(op_name, kernel::OpImplyType::kAKG);
  if (op_info_ptr == nullptr) {
    MS_LOG(ERROR) << "Not find op[" << op_name << "] in akg";
    return false;
  }
  if (!ParseMetadata(kernel_node, op_info_ptr, kernel::Processor::CUDA, &kernel_info_list)) {
    MS_LOG(EXCEPTION) << "Parsed metadata of op[" << op_name << "] failed.";
  }
  if (kernel_info_list.empty()) {
    MS_LOG(EXCEPTION) << "Akg dose not has metadata of op[" << op_name << "].";
  }

  bool match = std::any_of(kernel_info_list.begin(), kernel_info_list.end(),
                           [&](const std::shared_ptr<KernelBuildInfo> &alternative_kernel_info) {
                             return CheckKernelInfo(alternative_kernel_info, selected_kernel_info);
                           });
  if (!match) {
    MS_LOG(ERROR) << "Not find op[" << op_name << "] in akg";
    return false;
  }
  return true;
}

void SetTensorDeviceInfo(const kernel::KernelBuildInfo &selected_kernel_info, const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  for (size_t input_index = 0; input_index < AnfAlgo::GetInputTensorNum(kernel_node); ++input_index) {
    auto input_kernel_node = kernel_node->input(input_index + 1);
    MS_EXCEPTION_IF_NULL(input_kernel_node);
    if (!input_kernel_node->isa<Parameter>()) {
      continue;
    }
    std::shared_ptr<kernel::KernelBuildInfo::KernelBuildInfoBuilder> builder =
      std::make_shared<kernel::KernelBuildInfo::KernelBuildInfoBuilder>();

    auto param = input_kernel_node->cast<ParameterPtr>();
    MS_EXCEPTION_IF_NULL(param);
    if (!AnfAlgo::IsParameterWeight(param)) {
      std::vector<std::string> output_format = {kOpFormat_DEFAULT};
      builder->SetOutputsFormat(output_format);
      std::vector<TypeId> output_type = {AnfAlgo::GetOutputInferDataType(input_kernel_node, 0)};
      builder->SetOutputsDeviceType(output_type);
      AnfAlgo::SetSelectKernelBuildInfo(builder->Build(), input_kernel_node.get());
      continue;
    }
    if ((AnfAlgo::GetOutputDeviceDataType(input_kernel_node, 0) == kTypeUnknown) ||
        (AnfAlgo::GetCNodeName(kernel_node) == "ApplyMomentum")) {
      std::vector<std::string> output_format = {selected_kernel_info.GetInputFormat(input_index)};
      builder->SetOutputsFormat(output_format);
      std::vector<TypeId> output_type = {selected_kernel_info.GetInputDeviceType(input_index)};
      builder->SetOutputsDeviceType(output_type);
      AnfAlgo::SetSelectKernelBuildInfo(builder->Build(), input_kernel_node.get());
    }
  }
}

bool IsNeedProcessFormatInfo(const CNodePtr &kernel_node, const std::vector<TypeId> &inputs_type) {
  auto ms_context = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(ms_context);
  if (ms_context->get_param<int>(MS_CTX_EXECUTION_MODE) == kPynativeMode) {
    return false;
  }
  if (!FormatTransformChecker::GetInstance().format_transform()) {
    return false;
  }
  if (!AnfAlgo::IsRealCNodeKernel(kernel_node)) {
    return false;
  }
  auto kernel_name = AnfAlgo::GetCNodeName(kernel_node);
  auto iter = kKernelFormatPositionMap.find(kernel_name);
  if (iter == kKernelFormatPositionMap.end()) {
    return false;
  }
  if (inputs_type.size() == 0) {
    return false;
  }
  auto inputs_format_position = iter->second.first;
  // If input position is empty, then insert all the input positions, because the input numbers of this op are variable.
  if (inputs_format_position.size() == 0) {
    for (size_t input_index = 0; input_index < AnfAlgo::GetInputTensorNum(kernel_node); input_index++) {
      inputs_format_position.push_back(input_index);
    }
  }
  for (const auto &input_format_position : inputs_format_position) {
    auto input_shape = AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, input_format_position);
    if (input_shape.size() != 4) {
      return false;
    }
  }
  return true;
}

void UpdateKernelFormatInfo(const CNodePtr &kernel_node, const std::vector<TypeId> &inputs_type,
                            std::vector<std::string> *inputs_format, std::vector<std::string> *outputs_format,
                            std::string *origin_data_format) {
  auto kernel_name = AnfAlgo::GetCNodeName(kernel_node);
  auto iter = kKernelFormatPositionMap.find(kernel_name);
  if (iter == kKernelFormatPositionMap.end()) {
    return;
  }
  auto cal_format = (inputs_type[0] == kNumberTypeFloat16) ? kOpFormat_NHWC : kOpFormat_NCHW;
  MS_LOG(DEBUG) << "Kernel node: " << kernel_node->fullname_with_scope() << ", format: " << cal_format;
  auto inputs_format_position = iter->second.first;
  // If input position is empty, then insert all the input positions, because the input numbers of this op are variable.
  if (inputs_format_position.size() == 0) {
    for (size_t input_index = 0; input_index < AnfAlgo::GetInputTensorNum(kernel_node); input_index++) {
      inputs_format_position.push_back(input_index);
    }
  }
  for (const auto &input_format_position : inputs_format_position) {
    if (input_format_position >= inputs_format->size()) {
      MS_LOG(EXCEPTION) << "The position [" << input_format_position << "] is out of range of the input size ["
                        << inputs_format->size() << "] #kernel_node [" << kernel_node->fullname_with_scope() << "]";
    }
    (*inputs_format)[input_format_position] = cal_format;
  }
  auto outputs_format_position = iter->second.second;
  for (const auto &output_format_position : outputs_format_position) {
    if (output_format_position >= outputs_format->size()) {
      MS_LOG(EXCEPTION) << "The position [" << output_format_position << "] is out of range of the output size ["
                        << outputs_format->size() << "] #kernel_node [" << kernel_node->fullname_with_scope() << "]";
    }
    (*outputs_format)[output_format_position] = cal_format;
  }
  auto prim = AnfAlgo::GetCNodePrimitive(kernel_node);
  MS_EXCEPTION_IF_NULL(prim);
  if (prim->HasAttr("data_format")) {
    *origin_data_format = AnfAlgo::GetNodeAttr<std::string>(kernel_node, "data_format");
  }
}
}  // namespace

void FormatTransformChecker::CheckSupportFormatTransform(const std::shared_ptr<session::KernelGraph> &kernel_graph) {
  auto kernels = kernel_graph->execution_order();
  size_t conv_cnt = 0;
  size_t bn_cnt = 0;
  for (const auto &kernel : kernels) {
    auto kernel_name = AnfAlgo::GetCNodeName(kernel);
    if (kernel_name == prim::kPrimLayerNorm->name()) {
      format_transform_ = false;
      return;
    }
    if (kernel_name == prim::kPrimConv2D->name()) {
      conv_cnt++;
    }
    if (kernel_name == prim::kPrimFusedBatchNormEx->name()) {
      bn_cnt++;
    }
  }
  if (conv_cnt + bn_cnt > 1) {
    format_transform_ = true;
    return;
  }
  format_transform_ = false;
}

void SetKernelInfo(const CNodePtr &kernel_node) {
  std::vector<std::string> inputs_format;
  std::vector<TypeId> inputs_type;
  for (size_t input_index = 0; input_index < AnfAlgo::GetInputTensorNum(kernel_node); ++input_index) {
    inputs_format.emplace_back(kOpFormat_DEFAULT);
    inputs_type.push_back(AnfAlgo::GetPrevNodeOutputInferDataType(kernel_node, input_index));
  }
  std::vector<std::string> outputs_format;
  std::vector<TypeId> outputs_type;
  for (size_t output_index = 0; output_index < AnfAlgo::GetOutputTensorNum(kernel_node); ++output_index) {
    outputs_format.emplace_back(kOpFormat_DEFAULT);
    outputs_type.push_back(AnfAlgo::GetOutputInferDataType(kernel_node, output_index));
  }
  std::string origin_data_format = kOpFormat_DEFAULT;
  if (IsNeedProcessFormatInfo(kernel_node, inputs_type)) {
    UpdateKernelFormatInfo(kernel_node, inputs_type, &inputs_format, &outputs_format, &origin_data_format);
  }
  std::shared_ptr<KernelBuildInfo::KernelBuildInfoBuilder> builder =
    std::make_shared<KernelBuildInfo::KernelBuildInfoBuilder>();
  builder->SetOriginDataFormat(origin_data_format);
  builder->SetInputsFormat(inputs_format);
  builder->SetInputsDeviceType(inputs_type);
  builder->SetOutputsFormat(outputs_format);
  builder->SetOutputsDeviceType(outputs_type);

  bool result =
    kernel::GpuKernelFactory::GetInstance().SearchRegistered(AnfAlgo::GetCNodeName(kernel_node), builder->Build());
  KernelType kernel_type = UNKNOWN_KERNEL_TYPE;

  if (!result) {
    result = SelectAkgKernel(kernel_node, builder->Build());
    kernel_type = AKG_KERNEL;
  }

  if (!result) {
    auto kernel_name = AnfAlgo::GetCNodeName(kernel_node);
    std::string build_type = "in [";
    std::for_each(std::begin(inputs_type), std::end(inputs_type),
                  [&build_type](auto i) { build_type += mindspore::kernel::TypeId2String(i) + " "; });
    build_type += "] out [";
    std::for_each(std::begin(outputs_type), std::end(outputs_type),
                  [&build_type](auto i) { build_type += mindspore::kernel::TypeId2String(i) + " "; });
    build_type += "]";
    auto supported_type_lists = SupportedTypeList(kernel_node);
    MS_EXCEPTION(TypeError) << "Select GPU kernel op[" << kernel_name
                            << "] fail! Incompatible data type!\nThe supported data types are " << supported_type_lists
                            << ", but get " << build_type;
  }
  builder->SetKernelType(kernel_type);
  builder->SetProcessor(kernel::Processor::CUDA);
  AnfAlgo::SetSelectKernelBuildInfo(builder->Build(), kernel_node.get());
  SetTensorDeviceInfo(*(builder->Build()), kernel_node);
}
}  // namespace gpu
}  // namespace device
}  // namespace mindspore
