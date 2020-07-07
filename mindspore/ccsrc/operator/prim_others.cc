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

#include <string>
#include <sstream>

#include "ir/dtype.h"
#include "common/utils.h"
#include "operator/ops.h"
#include "pipeline/static_analysis/param_validator.h"
#include "pipeline/static_analysis/prim.h"
#include "pipeline/static_analysis/utils.h"
#include "utils/symbolic.h"
#include "utils/context/ms_context.h"

namespace mindspore {
namespace abstract {
AbstractBasePtr InferImplIdentity(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                  const AbstractBasePtrList &args_spec_list) {
  // An object of a subclass of AbstractBase
  CheckArgsSize(primitive->name(), args_spec_list, 1);
  return args_spec_list[0];
}

AbstractBasePtr InferImplJ(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                           const AbstractBasePtrList &args_spec_list) {
  // args: An object of AbstractFunction.
  CheckArgsSize(primitive->name(), args_spec_list, 1);
  MS_LOG(DEBUG) << "evaluate J: " << args_spec_list[0]->ToString();

  AbstractFunctionPtr x = dyn_cast<AbstractFunction>(args_spec_list[0]);
  if (x == nullptr) {
    return std::make_shared<AbstractJTagged>(args_spec_list[0]);
  }

  AbstractFuncAtomPtrList jv;
  auto build_jv = [&jv](const AbstractFuncAtomPtr &func) {
    auto j_closure = std::make_shared<JTransformedAbstractClosure>(func);
    jv.push_back(j_closure);
  };
  x->Visit(build_jv);

  return AbstractFunction::MakeAbstractFunction(jv);
}

class UndeterminedShapeType {
 public:
  explicit UndeterminedShapeType(const std::string &env_str) {
    // param_name indices_shape indices_type values_shape values_type dense_shape
    // export UNDETERMINED_SPARSE_SHAPE_TYPES="sparse_key_w1:2:Int32:2 1 2:Float32:3 1 2;sparse_key_w2:2:Int32:2 1
    // 2:Float32:3 1 2"
    std::vector<string> fields;
    string tmp;
    std::stringstream input(env_str);
    while (std::getline(input, tmp, ':')) {
      fields.push_back(tmp);
    }
    if (fields.size() != fields_num) {
      MS_LOG(EXCEPTION) << "Expect " << fields_num << " fields, but got " << fields.size();
    }

    param_name_ = fields[0];

    indices_shape_ = GetShape(fields[1]);
    indices_type_ = StringToType(fields[2]);

    values_shape_ = GetShape(fields[3]);
    values_type_ = StringToType(fields[4]);

    auto dense_shape_vec = GetShape(fields[5]);
    AbstractBasePtrList dense_shape_list;
    (void)std::transform(dense_shape_vec.begin(), dense_shape_vec.end(), std::back_inserter(dense_shape_list),
                         [](const auto &elem) { return FromValue(elem, false); });
    dense_shape_ = dense_shape_list;
  }
  ~UndeterminedShapeType() = default;
  const std::string &param_name() { return param_name_; }
  const std::vector<int> &indices_shape() { return indices_shape_; }
  const TypePtr &indices_type() { return indices_type_; }
  const std::vector<int> &values_shape() { return values_shape_; }
  const TypePtr &values_type() { return values_type_; }
  const AbstractBasePtrList &dense_shape() { return dense_shape_; }

 private:
  std::string param_name_;
  std::vector<int> indices_shape_;
  TypePtr indices_type_;
  std::vector<int> values_shape_;
  TypePtr values_type_;
  AbstractBasePtrList dense_shape_;
  static const size_t fields_num;

  std::vector<int> GetShape(const std::string &shape_str);
};
std::vector<int> UndeterminedShapeType::GetShape(const std::string &shape_str) {
  std::vector<int> ret;
  std::istringstream iss(shape_str);
  int elem;
  while (iss.good()) {
    iss >> elem;
    ret.emplace_back(elem);
  }
  return ret;
}
const size_t UndeterminedShapeType::fields_num = 6;

std::unordered_map<std::string, UndeterminedShapeType> g_undetermined_configs;
void InitUndeterminedFromEnv(const std::string &sparse_shape_types) {
  std::string tmp;
  std::stringstream input(sparse_shape_types);
  g_undetermined_configs.clear();
  while (std::getline(input, tmp, ';')) {
    auto config = UndeterminedShapeType(tmp);
    g_undetermined_configs.insert(std::make_pair(config.param_name(), config));
    MS_LOG(DEBUG) << "Undetermined config from env: " << tmp;
  }
}

AbstractBasePtr InferImplEnvGetItem(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                    const AbstractBasePtrList &args_spec_list) {
  MS_EXCEPTION_IF_NULL(primitive);
  // args: Three objects of a subclass of AbstractBase, env, key, dflt(default).
  CheckArgsSize(primitive->name(), args_spec_list, 3);
  auto key = args_spec_list[1];
  auto dflt = args_spec_list[2];
  TypePtr type = key->GetTypeTrack();
  MS_EXCEPTION_IF_NULL(type);
  if (type->type_id() != kObjectTypeSymbolicKeyType) {
    MS_LOG(EXCEPTION) << "EnvGetItem evaluator args[1] should be a SymbolicKeyInstance but: " << key->ToString();
  }

  if (!key->sparse_grad().empty()) {
    // Will be fixed once undetermined type ready
    if (g_undetermined_configs.empty()) {
      auto sparse_shape_types = common::GetEnv("UNDETERMINED_SPARSE_SHAPE_TYPES");
      MS_LOG(INFO) << "Undetermind sparse shape:" << sparse_shape_types;
      if (sparse_shape_types.empty()) {
        sparse_shape_types = "sparse_key_w1:2:Int32:2 1 2:Float32:3 1 2;sparse_key_w2:2:Int32:2 1 2:Float32:3 1 2";
      }
      InitUndeterminedFromEnv(sparse_shape_types);
    }

    auto shape_types = g_undetermined_configs.find(key->sparse_grad());
    if (shape_types == g_undetermined_configs.end()) {
      MS_LOG(EXCEPTION) << "Param " << key->ToString()
                        << " has sparse_grad, but shape/type is not configured in env UNDETERMINED_SPARSE_SHAPE_TYPES";
    }
    MS_LOG(DEBUG) << "EnvGetItem is sparse_grad " << key->ToString();
    AbstractBasePtrList sparse_list;
    // indices
    auto indices_ele = std::make_shared<AbstractScalar>(kAnyValue, shape_types->second.indices_type());
    auto indices =
      std::make_shared<AbstractTensor>(indices_ele, std::make_shared<Shape>(shape_types->second.indices_shape()));
    sparse_list.emplace_back(indices);
    // values
    auto dout_ele = std::make_shared<AbstractScalar>(kAnyValue, shape_types->second.values_type());
    auto dout = std::make_shared<AbstractTensor>(dout_ele, std::make_shared<Shape>(shape_types->second.values_shape()));
    sparse_list.emplace_back(dout);
    // dense_shape
    sparse_list.emplace_back(std::make_shared<AbstractTuple>(shape_types->second.dense_shape()));
    return std::make_shared<AbstractTuple>(sparse_list);
  }

  auto context = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context);
  bool enable_sparse_flag = context->enable_sparse_flag();
  if (enable_sparse_flag && key->has_indexed_slices_grad() && dflt->isa<AbstractTensor>()) {
    auto dflt_tensor = dflt->cast<AbstractTensorPtr>();
    return std::make_shared<AbstractUndetermined>(dflt_tensor->element()->Clone(), dflt_tensor->shape()->Clone());
  }
  if (!key->GetValueTrack()->isa<SymbolicKeyInstance>()) {
    return dflt;
  }
  ValuePtr key_value_ptr = key->GetValueTrack();
  MS_EXCEPTION_IF_NULL(key_value_ptr);
  auto key_value_track = key_value_ptr->cast<SymbolicKeyInstancePtr>();
  auto expected = key_value_track->abstract();
  MS_EXCEPTION_IF_NULL(expected);
  (void)expected->Join(dflt);
  return expected;
}

AbstractBasePtr InferImplEnvSetItem(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                    const AbstractBasePtrList &args_spec_list) {
  // args: Three objects of a subclass of AbstractBase, env, key, dflt(default).
  CheckArgsSize(primitive->name(), args_spec_list, 3);

  auto key = args_spec_list[1];
  ValuePtr key_value_ptr = key->GetValueTrack();
  MS_EXCEPTION_IF_NULL(key_value_ptr);
  auto key_value_track = key_value_ptr->cast<SymbolicKeyInstancePtr>();
  if (key_value_track == nullptr) {
    MS_LOG(EXCEPTION) << "EnvGetItem evaluator args[1] expected should be able to cast to SymbolicKeyInstancePtrbut: "
                      << key_value_ptr->ToString();
  }
  auto expected = key_value_track->abstract();
  MS_EXCEPTION_IF_NULL(expected);
  return std::make_shared<AbstractScalar>(kAnyValue, std::make_shared<EnvType>());
}

AbstractBasePtr InferImplEnvAdd(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                const AbstractBasePtrList &args_spec_list) {
  // args: Three objects of a subclass of AbstractBase, env, key, dflt(default).
  CheckArgsSize(primitive->name(), args_spec_list, 2);
  return std::make_shared<AbstractScalar>(kAnyValue, std::make_shared<EnvType>());
}

AbstractBasePtr InferImplMakeRefKey(const AnalysisEnginePtr &, const PrimitivePtr &prim, const AbstractBasePtrList &) {
  ValuePtr name_value = prim->GetAttr("tag");
  auto name = name_value->cast<StringImmPtr>();
  if (name == nullptr) {
    MS_LOG(EXCEPTION) << "MakeRefKey attr tag sould be a String " << name_value->ToString() << ".";
  }
  auto refkey = std::make_shared<RefKey>(name->value());
  if (refkey == nullptr) {
    MS_LOG(EXCEPTION) << "MakeRefKey std::make_shared<RefKey> failed";
  }
  return refkey->ToAbstract();
}

AbstractBasePtr InferImplMakeRef(const AnalysisEnginePtr &, const PrimitivePtr &,
                                 const AbstractBasePtrList &args_spec_list) {
  // arguments: key, value, original value
  if (args_spec_list.size() != 3) {
    MS_LOG(EXCEPTION) << "make_ref evaluator requires 3 parameters, while the input size is " << args_spec_list.size()
                      << ".";
  }
  TypePtr type = args_spec_list[0]->GetTypeTrack();
  if (type->type_id() != kObjectTypeRefKey) {
    MS_LOG(EXCEPTION) << "First input of make_ref should be a RefKey but a " << type->ToString();
  }
  auto ret = std::make_shared<AbstractRef>(args_spec_list[0], args_spec_list[1], args_spec_list[2]);
  ret->set_sparse_grad(args_spec_list[2]->sparse_grad());
  ret->set_has_indexed_slices_grad(args_spec_list[2]->has_indexed_slices_grad());
  return ret;
}

AbstractBasePtr InferImplGetRefKey(const AnalysisEnginePtr &, const PrimitivePtr &,
                                   const AbstractBasePtrList &args_spec_list) {
  // arguments: value
  if (args_spec_list.size() != 1) {
    MS_LOG(EXCEPTION) << "get_ref_key requires 1 parameters, while the input size is " << args_spec_list.size() << ".";
  }
  TypePtr type = args_spec_list[0]->GetTypeTrack();
  if (type->type_id() != kObjectTypeRef) {
    MS_LOG(EXCEPTION) << "First input of get_ref_key should be a Ref but a " << type->ToString();
  }
  return args_spec_list[0]->cast<AbstractRefPtr>()->ref();
}

AbstractBasePtr InferImplGetRefValue(const AnalysisEnginePtr &, const PrimitivePtr &,
                                     const AbstractBasePtrList &args_spec_list) {
  // arguments: value
  if (args_spec_list.size() != 1) {
    MS_LOG(EXCEPTION) << "get_ref_value requires 1 parameters, while the input size is " << args_spec_list.size()
                      << ".";
  }
  TypePtr type = args_spec_list[0]->GetTypeTrack();
  if (type->type_id() != kObjectTypeRef) {
    MS_LOG(EXCEPTION) << "First input of get_ref_value should be a Ref but a " << type->ToString();
  }
  return args_spec_list[0]->cast<AbstractRefPtr>()->ref();
}

AbstractBasePtr InferImplGetRefOrigin(const AnalysisEnginePtr &, const PrimitivePtr &,
                                      const AbstractBasePtrList &args_spec_list) {
  // arguments: value
  if (args_spec_list.size() != 1) {
    MS_LOG(EXCEPTION) << "get_ref_origin requires 1 parameters, while the input size is " << args_spec_list.size()
                      << ".";
  }
  TypePtr type = args_spec_list[0]->GetTypeTrack();
  if (type->type_id() != kObjectTypeRef) {
    MS_LOG(EXCEPTION) << "First input of get_ref_value should be a Ref but a " << type->ToString();
  }
  return args_spec_list[0]->cast<AbstractRefPtr>()->ref_origin();
}

AbstractBasePtr InferImplStateSetItem(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                      const AbstractBasePtrList &args_spec_list) {
  // args: Two objects of a subclass of AbstractBase, key and value.
  CheckArgsSize(primitive->name(), args_spec_list, 2);

  TypePtr type = args_spec_list[0]->GetTypeTrack();
  MS_EXCEPTION_IF_NULL(type);
  if (type->type_id() != kObjectTypeRefKey && type->type_id() != kObjectTypeSymbolicKeyType) {
    MS_LOG(EXCEPTION) << "First input of StateSetItem should be a RefKey or SymbolicKeyType but a " << type->ToString();
  }
  return std::make_shared<AbstractScalar>(kAnyValue, kBool);
}

AbstractBasePtr InferImplDepend(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                const AbstractBasePtrList &args_spec_list) {
  if (args_spec_list.empty()) {
    MS_LOG(EXCEPTION) << primitive->name() << " input args size should be at lest 1, but got 0";
  }
  auto depends = args_spec_list[0]->Broaden();
  return depends;
}

bool CompareShape(const std::vector<ValuePtr> &x_shape, const std::vector<ValuePtr> &y_shape) {
  if (x_shape.size() != y_shape.size()) {
    return false;
  }

  for (size_t i = 0; i < x_shape.size(); ++i) {
    if (GetValue<int>(x_shape[i]) != GetValue<int>(y_shape[i])) {
      return false;
    }
  }

  return true;
}

enum State {
  SAME,
  X_ONE,
  Y_ONE,
};

void ComputeReduceIndex(const std::vector<int> &reverse_x, const std::vector<int> &reverse_y,
                        std::vector<int> *grad_x_reduce_idx, std::vector<int> *grad_y_reduce_idy) {
  const size_t n = reverse_x.size();
  for (size_t i = 0; i < n; ++i) {
    State curr;
    const int32_t x_i = reverse_x[i];
    const int32_t y_i = reverse_y[i];
    const int reduce_idx = SizeToInt(n - 1 - i);
    if (x_i == y_i) {
      curr = SAME;
    } else if (x_i == 1) {
      grad_x_reduce_idx->push_back(reduce_idx);
      curr = X_ONE;
    } else if (y_i == 1) {
      grad_y_reduce_idy->push_back(reduce_idx);
      curr = Y_ONE;
    } else {
      MS_LOG(EXCEPTION) << "not compatible shape input for BroadcastGradientArgs";
    }
    if (curr == SAME && x_i == 1) {
      grad_x_reduce_idx->push_back(reduce_idx);
      grad_y_reduce_idy->push_back(reduce_idx);
      continue;
    }
  }

  std::reverse(grad_x_reduce_idx->begin(), grad_x_reduce_idx->end());
  std::reverse(grad_y_reduce_idy->begin(), grad_y_reduce_idy->end());
}

AbstractBasePtr BroadcastGradientArgsDiff(const std::vector<ValuePtr> &x_shape, const std::vector<ValuePtr> &y_shape) {
  std::vector<int> reverse_x;
  std::vector<int> reverse_y;

  (void)std::transform(x_shape.rbegin(), x_shape.rend(), std::back_inserter(reverse_x),
                       [](const ValuePtr &v) { return v->cast<Int32ImmPtr>()->value(); });
  (void)std::transform(y_shape.rbegin(), y_shape.rend(), std::back_inserter(reverse_y),
                       [](const ValuePtr &v) { return v->cast<Int32ImmPtr>()->value(); });

  if (reverse_x.size() > reverse_y.size()) {
    reverse_y.resize(reverse_x.size(), 1);
  } else {
    reverse_x.resize(reverse_y.size(), 1);
  }

  std::vector<int> grad_x_reduce_idx;
  std::vector<int> grad_y_reduce_idy;
  ComputeReduceIndex(reverse_x, reverse_y, &grad_x_reduce_idx, &grad_y_reduce_idy);

  AbstractBasePtrList abs_list_x;
  AbstractBasePtrList abs_list_y;
  (void)std::transform(grad_x_reduce_idx.begin(), grad_x_reduce_idx.end(), std::back_inserter(abs_list_x),
                       [](int v) { return abstract::FromValue(v); });
  (void)std::transform(grad_y_reduce_idy.begin(), grad_y_reduce_idy.end(), std::back_inserter(abs_list_y),
                       [](int v) { return abstract::FromValue(v); });
  auto x_reduce_idx = std::make_shared<AbstractTuple>(abs_list_x);
  auto y_reduce_idx = std::make_shared<AbstractTuple>(abs_list_y);
  AbstractBasePtrList elem_list;
  elem_list.push_back(x_reduce_idx);
  elem_list.push_back(y_reduce_idx);

  return std::make_shared<AbstractTuple>(elem_list);
}

AbstractBasePtr InferImplBroadcastGradientArgs(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                               const AbstractBasePtrList &args_spec_list) {
  // this primitive get the index that need to reduce
  // input: x's shape and y's shape, inputs should be tuple
  // output: tuple of x and y 's reduce index, reduce index should be a tuple
  const std::string op_name = primitive->name();
  CheckArgsSize(op_name, args_spec_list, 2);
  auto arg_x = CheckArg<AbstractTuple>(op_name, args_spec_list, 0);
  auto arg_y = CheckArg<AbstractTuple>(op_name, args_spec_list, 1);

  ValueTuplePtr arg_x_value = arg_x->BuildValue()->cast<ValueTuplePtr>();
  MS_EXCEPTION_IF_NULL(arg_x_value);

  ValueTuplePtr arg_y_value = arg_y->BuildValue()->cast<ValueTuplePtr>();
  MS_EXCEPTION_IF_NULL(arg_y_value);

  const std::vector<ValuePtr> x_shape = arg_x_value->value();
  const std::vector<ValuePtr> y_shape = arg_y_value->value();
  bool is_same_shape = CompareShape(x_shape, y_shape);
  // if it is the same shape , do not need reduce , return empty tuple
  if (is_same_shape) {
    AbstractBasePtrList empty_list;
    auto x_reduce_idx = std::make_shared<AbstractTuple>(empty_list);
    auto y_reduce_idx = std::make_shared<AbstractTuple>(empty_list);

    AbstractBasePtrList elem_list;
    elem_list.push_back(x_reduce_idx);
    elem_list.push_back(y_reduce_idx);

    return std::make_shared<AbstractTuple>(elem_list);
  }

  return BroadcastGradientArgsDiff(x_shape, y_shape);
}

AbstractBasePtr InferImplControlDepend(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                       const AbstractBasePtrList &args_spec_list) {
  // args: Two objects of a subclass of AbstractBase
  CheckArgsSize(primitive->name(), args_spec_list, 2);
  auto arg_src = args_spec_list[0];
  auto arg_dst = args_spec_list[1];
  // control depend can not setup tuple of ops to tuple of ops dependency relation
  if (arg_src->isa<AbstractTuple>() && arg_dst->isa<AbstractTuple>()) {
    auto src_size = arg_src->cast<AbstractTuplePtr>()->size();
    auto dst_size = arg_src->cast<AbstractTuplePtr>()->size();
    if (src_size > 1 && dst_size > 1) {
      MS_LOG(EXCEPTION) << "Control depend can not setup operator dependcy relationship from tuple from tuple";
    }
  }
  return std::make_shared<AbstractScalar>(kAnyValue, kBool);
}

AbstractBasePtr InferImplMakeIndexedSlices(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                           const AbstractBasePtrList &args_spec_list) {
  // Inputs: two tensors and a tuple.
  const std::string op_name = primitive->name();
  CheckArgsSize(op_name, args_spec_list, 3);
  auto indices = CheckArg<AbstractTensor>(op_name, args_spec_list, 0);
  auto values = CheckArg<AbstractTensor>(op_name, args_spec_list, 1);
  auto dense_shape = CheckArg<AbstractTuple>(op_name, args_spec_list, 2);

  auto dense_shape_value = dense_shape->BuildValue()->cast<ValueTuplePtr>();
  MS_EXCEPTION_IF_NULL(dense_shape_value);
  auto shp = dense_shape_value->value();
  std::vector<int> dense_shape_vec;
  (void)std::transform(std::begin(shp), std::end(shp), std::back_inserter(dense_shape_vec),
                       [](const ValuePtr &e) -> int {
                         auto elem = GetValue<int>(e);
                         return elem;
                       });
  auto ret = std::make_shared<AbstractIndexedSlices>(values->element()->BuildType(), dense_shape_vec);
  ret->set_indices(indices);
  ret->set_values(values);
  ret->set_dense_shape(dense_shape);
  return ret;
}

AbstractBasePtr InferImplIndexedSlicesGetValues(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                                const AbstractBasePtrList &args_spec_list) {
  // Inputs: two tensors and a tuple.
  const std::string op_name = primitive->name();
  CheckArgsSize(op_name, args_spec_list, 1);
  auto indexed_slices = CheckArg<AbstractIndexedSlices>(op_name, args_spec_list, 0);
  MS_EXCEPTION_IF_NULL(indexed_slices->values());
  return indexed_slices->values();
}

AbstractBasePtr InferImplIndexedSlicesGetIndices(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                                 const AbstractBasePtrList &args_spec_list) {
  // Inputs: two tensors and a tuple.
  const std::string op_name = primitive->name();
  CheckArgsSize(op_name, args_spec_list, 1);
  auto indexed_slices = CheckArg<AbstractIndexedSlices>(op_name, args_spec_list, 0);
  MS_EXCEPTION_IF_NULL(indexed_slices->indices());
  return indexed_slices->indices();
}

AbstractBasePtr InferImplIndexedSlicesGetDenseShape(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                                    const AbstractBasePtrList &args_spec_list) {
  // Inputs: two tensors and a tuple.
  const std::string op_name = primitive->name();
  CheckArgsSize(op_name, args_spec_list, 1);
  auto indexed_slices = CheckArg<AbstractIndexedSlices>(op_name, args_spec_list, 0);
  MS_EXCEPTION_IF_NULL(indexed_slices->dense_shape());
  return indexed_slices->dense_shape();
}

AbstractBasePtr InferImplIsIndexedSlices(const AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                         const AbstractBasePtrList &args_spec_list) {
  const std::string op_name = primitive->name();
  CheckArgsSize(op_name, args_spec_list, 1);
  bool ret = false;
  if (args_spec_list[0]->isa<AbstractIndexedSlices>()) {
    ret = true;
  }
  MS_LOG(DEBUG) << "IsIndexedSlices result: " << ret << ", input: " << args_spec_list[0]->ToString();
  return std::make_shared<AbstractScalar>(ret);
}
}  // namespace abstract
}  // namespace mindspore
