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

#include "tools/converter/converter_flags.h"
#include <regex>
#include <string>
#include "ir/dtype/type_id.h"

namespace mindspore {
namespace lite {
namespace converter {
Flags::Flags() {
  AddFlag(&Flags::fmkIn, "fmk", "Input model framework type. TFLITE | CAFFE | MS", "");
  AddFlag(&Flags::modelFile, "modelFile",
          "Input model file path. TFLITE: *.tflite | CAFFE: *.prototxt | MS: *.mindir | ONNX: *.onnx", "");
  AddFlag(&Flags::outputFile, "outputFile", "Output model file path. Will add .ms automatically", "");
  AddFlag(&Flags::weightFile, "weightFile",
          "Input model weight file path. Needed when fmk is CAFFE. CAFFE: *.caffemodel", "");
  AddFlag(&Flags::inferenceTypeIn, "inferenceType",
          "Real data type saved in output file, reserved param, NOT used for now. FLOAT | INT8", "FLOAT");
  AddFlag(&Flags::quantTypeIn, "quantType", "Quantization Type. AwareTraining | PostTraining", "");
  AddFlag(&Flags::inputInferenceTypeIn, "inputInferenceType", "Input inference data type. FLOAT | INT8", "FLOAT");
  AddFlag(&Flags::stdDev, "stdDev", "Standard deviation value for aware-quantization", "128");
  AddFlag(&Flags::mean, "mean", "Mean value for aware-quantization", "-0.5");
  AddFlag(&Flags::bitNum, "bitNum", "Weight quantization bitNum", "8");
  AddFlag(&Flags::quantSize, "quantSize", "Weight quantization size threshold", "0");
  AddFlag(&Flags::convWeightQuantChannelThreshold, "convWeightQuantChannelThreshold", "convWeightQuantChannelThreshold",
          "16");
  AddFlag(&Flags::configFile, "config_file", "Configuration for post-training.", "");
  AddFlag(&Flags::formatTrans, "formatTrans", "whether transform format. true | false", "true");
}

int Flags::Init(int argc, const char **argv) {
  if (argc == 1) {
    std::cout << this->Usage() << std::endl;
    return RET_SUCCESS_EXIT;
  }
  Option<std::string> err = this->ParseFlags(argc, argv);

  if (err.IsSome()) {
    std::cerr << err.Get();
    std::cerr << this->Usage() << std::endl;
    return 1;
  }

  if (this->help) {
    std::cout << this->Usage() << std::endl;
    return RET_SUCCESS_EXIT;
  }
  if (this->modelFile.empty()) {
    std::cerr << "INPUT MISSING: model file path is necessary";
    return 1;
  }
  if (this->outputFile.empty()) {
    std::cerr << "INPUT MISSING: output file path is necessary";
    return 1;
  }

  if (this->outputFile.rfind('/') == this->outputFile.length() - 1) {
    std::cerr << "INPUT ILLEGAL: outputFile must be a valid file path";
    return 1;
  }

  if (this->fmkIn.empty()) {
    std::cerr << "INPUT MISSING: fmk is necessary";
    return 1;
  }
  if (this->inputInferenceTypeIn == "FLOAT") {
    this->inputInferenceType = TypeId::kNumberTypeFloat;
  } else if (this->inputInferenceTypeIn == "INT8") {
    this->inputInferenceType = TypeId::kNumberTypeInt8;
  } else {
    std::cerr << "INPUT INVALID: inputInferenceType is invalid: %s, supported inputInferenceType: FLOAT | INT8",
      this->inputInferenceTypeIn.c_str();
    return 1;
  }

  if (this->inferenceTypeIn == "FLOAT") {
    this->inferenceType = TypeId::kNumberTypeFloat;
  } else if (this->inferenceTypeIn == "INT8") {
    this->inferenceType = TypeId::kNumberTypeInt8;
  } else {
    std::cerr << "INPUT INVALID: inferenceType is invalid: %s, supported inferenceType: FLOAT | INT8",
      this->inferenceTypeIn.c_str();
    return 1;
  }

  if (this->fmkIn == "CAFFE") {
    this->fmk = FmkType_CAFFE;
  } else if (this->fmkIn == "MS") {
    this->fmk = FmkType_MS;
  } else if (this->fmkIn == "TFLITE") {
    this->fmk = FmkType_TFLITE;
  } else if (this->fmkIn == "ONNX") {
    this->fmk = FmkType_ONNX;
  } else {
    std::cerr << "INPUT ILLEGAL: fmk must be TFLITE|CAFFE|MS|ONNX";
    return 1;
  }

  if (this->fmk != FmkType_CAFFE && !weightFile.empty()) {
    std::cerr << "INPUT ILLEGAL: weightFile is not a valid flag";
    return 1;
  }
  if (this->quantTypeIn == "AwareTraining") {
    this->quantType = QuantType_AwareTraining;
  } else if (this->quantTypeIn == "WeightQuant") {
    this->quantType = QuantType_WeightQuant;
  } else if (this->quantTypeIn == "PostTraining") {
    this->quantType = QuantType_PostTraining;
  } else if (this->quantTypeIn.empty()) {
    this->quantType = QuantType_QUANT_NONE;
  } else {
    std::cerr << "INPUT ILLEGAL: quantType must be AwareTraining|WeightQuant|PostTraining";
    return 1;
  }

  return 0;
}
}  // namespace converter
}  // namespace lite
}  // namespace mindspore
