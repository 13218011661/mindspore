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
#ifndef MINDSPORE_CCSRC_PRE_ACTIVATE_ASCEND_IR_FUSION_ADAM_APPLY_ONE_FUSION_H_
#define MINDSPORE_CCSRC_PRE_ACTIVATE_ASCEND_IR_FUSION_ADAM_APPLY_ONE_FUSION_H_

#include <vector>
#include <memory>
#include "pre_activate/common/optimizer.h"
#include "utils/utils.h"

namespace mindspore {
namespace opt {
constexpr size_t kAdamApplyOneInputNum = 5;
constexpr size_t kAdamApplyOneMulInputNum = 4;

class AdamApplyOneFusion : public PatternProcessPass {
 public:
  explicit AdamApplyOneFusion(bool multigraph = true) : PatternProcessPass("adam_apply_one_fusion", multigraph) {
    for (size_t i = 0; i < kAdamApplyOneInputNum; ++i) {
      input_vars_.push_back(std::make_shared<Var>());
    }
    for (size_t i = 0; i < kAdamApplyOneMulInputNum; ++i) {
      mul_x_input_vars_.push_back(std::make_shared<Var>());
    }
    add2_y_ = std::make_shared<Var>();
    add0_var_ = std::make_shared<Var>(std::make_shared<Primitive>(prim::kPrimTensorAdd->name()));
    add1_var_ = std::make_shared<Var>(std::make_shared<Primitive>(prim::kPrimTensorAdd->name()));
  }

  ~AdamApplyOneFusion() override = default;
  const BaseRef DefinePattern() const override;
  const AnfNodePtr Process(const FuncGraphPtr &, const AnfNodePtr &, const EquivPtr &) const override;

 private:
  AnfNodePtr CreateAdamApplyOneNode(const FuncGraphPtr &func_graph, const EquivPtr &equiv) const;
  std::vector<VarPtr> input_vars_;
  std::vector<VarPtr> mul_x_input_vars_;
  VarPtr add2_y_;
  VarPtr add0_var_;
  VarPtr add1_var_;
};
}  // namespace opt
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_PRE_ACTIVATE_ASCEND_IR_FUSION_ADAM_APPLY_ONE_FUSION_H_
