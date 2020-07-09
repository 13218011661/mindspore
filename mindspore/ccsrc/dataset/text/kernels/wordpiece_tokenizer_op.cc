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

#include "dataset/text/kernels/wordpiece_tokenizer_op.h"
#include <algorithm>
#include <utility>

namespace mindspore {
namespace dataset {

const char WordpieceTokenizerOp::kDefSuffixIndicator[] = "##";
const int WordpieceTokenizerOp::kDefMaxBytesPerToken = 100;
const char WordpieceTokenizerOp::kDefUnknownToken[] = "[UNK]";

WordpieceTokenizerOp::WordpieceTokenizerOp(const std::shared_ptr<Vocab> &vocab, const std::string &suffix_indicator,
                                           const int &max_bytes_per_token, const std::string &unknown_token)
    : vocab_(vocab),
      suffix_indicator_(suffix_indicator),
      max_bytes_per_token_(max_bytes_per_token),
      unknown_token_(unknown_token) {}

Status WordpieceTokenizerOp::LookupWord(const std::string &input_token, const RuneStrArray &runes, const int start,
                                        bool *out_found, int *out_end) const {
  CHECK_FAIL_RETURN_UNEXPECTED(start >= 0 && start < input_token.size(), "Out of range");
  *out_found = false;
  for (int i = runes.size() - 1; i >= 0; i--) {
    *out_end = runes[i].offset + runes[i].len;
    int len = *out_end - start;
    std::string word = input_token.substr(start, len);
    if (start > 0) {
      word = suffix_indicator_ + word;
    }
    if (vocab_->Lookup(word) != Vocab::kNoTokenExists) {
      *out_found = true;
      break;
    }
  }
  return Status::OK();
}

Status WordpieceTokenizerOp::FoundNoToken(const std::string &input_token, std::vector<std::string> *out_tokens) const {
  out_tokens->clear();
  if (unknown_token_.empty()) {
    out_tokens->emplace_back(input_token);
  } else {
    out_tokens->emplace_back(unknown_token_);
  }
  return Status::OK();
}

Status WordpieceTokenizerOp::AddSubword(const std::string &input_token, const int start, const int end,
                                        std::vector<std::string> *out_tokens) const {
  CHECK_FAIL_RETURN_UNEXPECTED(start >= 0 && end > start && end <= input_token.size(), "Out of range");
  std::string subword = input_token.substr(start, end - start);
  if (start > 0) {
    subword = suffix_indicator_ + subword;
  }
  out_tokens->emplace_back(subword);
  return Status::OK();
}

Status WordpieceTokenizerOp::GetTokens(const std::string &input_token, std::vector<std::string> *out_tokens) const {
  if (input_token.size() > max_bytes_per_token_) {
    return FoundNoToken(input_token, out_tokens);
  }
  RuneStrArray runes;
  if (!DecodeRunesInString(input_token.data(), input_token.size(), runes)) {
    RETURN_STATUS_UNEXPECTED("Decode utf8 string failed.");
  }
  int end = 0;
  for (int start = 0; start < input_token.size();) {
    bool found = false;
    RETURN_IF_NOT_OK(LookupWord(input_token, runes, start, &found, &end));
    if (found) {
      RETURN_IF_NOT_OK(AddSubword(input_token, start, end, out_tokens));
      start = end;
    } else {
      return FoundNoToken(input_token, out_tokens);
    }
  }
  return Status::OK();
}

Status WordpieceTokenizerOp::Compute(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output) {
  IO_CHECK(input, output);
  if (input->Rank() > 1 || input->type() != DataType::DE_STRING) {
    RETURN_STATUS_UNEXPECTED("The input tensor should be scalar or 1-D string tensor");
  }
  std::vector<std::string> out_tokens;
  for (auto iter = input->begin<std::string_view>(); iter != input->end<std::string_view>(); iter++) {
    std::vector<std::string> temp_tokens;
    RETURN_IF_NOT_OK(GetTokens(std::string(*iter), &temp_tokens));
    out_tokens.insert(out_tokens.end(), temp_tokens.begin(), temp_tokens.end());
  }
  if (out_tokens.empty()) {
    out_tokens.emplace_back("");
  }
  *output = std::make_shared<Tensor>(out_tokens, TensorShape({(dsize_t)out_tokens.size()}));
  return Status::OK();
}

}  // namespace dataset
}  // namespace mindspore
