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
#include <stdint.h>
#include <float.h>
#include "nnacl/fp32_grad/pooling_grad.h"

void AvgPoolingGrad(const float *input_ptr, float *output_ptr, PoolingParameter *pooling_param) {
  int stride_w = pooling_param->stride_w_;
  int stride_h = pooling_param->stride_h_;
  int pad_w = pooling_param->pad_l_;
  int pad_h = pooling_param->pad_u_;
  int win_w = pooling_param->window_w_;
  int win_h = pooling_param->window_h_;
  int channel = pooling_param->input_channel_;
  int in_w = pooling_param->input_w_;
  int in_h = pooling_param->input_h_;
  int output_w = pooling_param->output_w_;
  int output_h = pooling_param->output_h_;
  int output_batch = pooling_param->output_batch_;

  const float *inPtr = NULL;
  // for (int i = 0; i < output_h * output_w * channel * output_batch; i++) output_ptr[i] = 0.0;
  for (int i = 0; i < in_h * in_w * channel * output_batch; i++) output_ptr[i] = 0.0;

  float kk = (float)(win_h * win_w);

  for (uint16_t ib = 0; ib < output_batch; ib++) {
    float *out;
    // out = &output_ptr[(ib * output_h * output_w)];
    out = &output_ptr[(ib * in_h * in_w * channel)];
    // inPtr = (float *)(&input_ptr[(ib * in_h * in_w)]);
    inPtr = (float *)(&input_ptr[(ib * output_h * output_w * channel)]);
    if (1) {  // in->layout() == Tensor::nhwc)
      // iterate over yt
      for (uint16_t yh = 0; yh < output_h; yh++) {
        for (uint16_t yw = 0; yw < output_w; yw++) {
          for (uint16_t ic = 0; ic < channel; ic++) {
            int idx = (yw + yh * output_w) * channel + ic;  // (ic*in_h*in_w) + (in_w*yh) + yw;
            float delta = inPtr[idx] / kk;
            for (int32_t kh = 0; kh < win_h; kh++) {
              int xh = yh * stride_h + kh - pad_h;
              if ((xh < 0) || (xh >= in_h)) {
                continue;
              }
              for (int32_t kw = 0; kw < win_w; kw++) {
                int xw = yw * stride_w + kw - pad_w;
                if ((xw < 0) || (xw >= in_w)) {
                  continue;
                }

                // out[(xw + output_w * xh) * channel + ic] += delta;
                out[(xw + in_w * xh) * channel + ic] += delta;
              }
            }
          }
        }
      }
    } else {  // nchw
      for (uint16_t ic = 0; ic < channel; ic++) {
        // iterate over yt
        for (uint16_t yh = 0; yh < output_h; yh++) {
          for (uint16_t yw = 0; yw < output_w; yw++) {
            int idx = (ic * output_h * output_w) + (output_w * yh) + yw;
            float delta = inPtr[idx] / kk;
            for (int32_t kh = 0; kh < win_h; kh++) {
              int xh = yh * stride_h + kh - pad_h;
              if ((xh < 0) || (xh >= in_h)) {
                continue;
              }
              for (int32_t kw = 0; kw < win_w; kw++) {
                int xw = yw * stride_w + kw - pad_w;
                if ((xw < 0) || (xw >= in_w)) {
                  continue;
                }
                // out[(ic * output_h * output_w) + (xh * output_w) + xw] += delta;
                out[(ic * in_h * in_w) + (xh * in_w) + xw] += delta;
              }
            }
          }
        }
      }
    }
  }
}

void MaxPoolingGrad(const float *input_ptr, const float *dx_ptr, const float *dy_ptr, float *output_ptr,
                    PoolingParameter *pooling_param) {
  int stride_w = pooling_param->stride_w_;
  int stride_h = pooling_param->stride_h_;
  int pad_w = pooling_param->pad_l_;
  int pad_h = pooling_param->pad_u_;
  int win_w = pooling_param->window_w_;
  int win_h = pooling_param->window_h_;
  int channel = pooling_param->input_channel_;
  int in_w = pooling_param->input_w_;
  int in_h = pooling_param->input_h_;
  int output_w = pooling_param->output_w_;
  int output_h = pooling_param->output_h_;
  int output_batch = pooling_param->output_batch_;

  const float *inPtr;
  const float *dyPtr;

  for (int i = 0; i < in_h * in_w * channel * output_batch; i++) output_ptr[i] = 0.0;

  for (uint16_t ib = 0; ib < output_batch; ib++) {
    float *out;
    out = &output_ptr[(ib * in_h * in_w * channel)];
    inPtr = (const float *)(&input_ptr[(ib * in_h * in_w * channel)]);
    dyPtr = (const float *)(&dy_ptr[(ib * output_h * output_w * channel)]);

    if (1) {  // nhwc
      for (uint16_t yh = 0; yh < output_h; yh++) {
        for (uint16_t yw = 0; yw < output_w; yw++) {
          for (uint16_t ic = 0; ic < channel; ic++) {
            int idx = (yw + yh * output_w) * channel + ic;

            float delta = dyPtr[idx];
            float max_val = -FLT_MAX;
            int max_idx = 0;
            for (int32_t kh = 0; kh < win_h; kh++) {
              int xh = yh * stride_h + kh - pad_h;
              if ((xh < 0) || (xh >= in_h)) {
                continue;
              }
              for (int32_t kw = 0; kw < win_w; kw++) {
                int xw = yw * stride_w + kw - pad_w;
                if ((xw < 0) || (xw >= in_w)) {
                  continue;
                }

                if (inPtr[(xw + in_w * xh) * channel + ic] > max_val) {
                  max_val = inPtr[(xw + in_w * xh) * channel + ic];
                  max_idx = (xw + in_w * xh) * channel + ic;
                }
              }
            }
            out[max_idx] += delta;
          }
        }
      }
    } else {  // nchw
      for (uint16_t yh = 0; yh < output_h; yh++) {
        for (uint16_t yw = 0; yw < output_w; yw++) {
          for (uint16_t ic = 0; ic < channel; ic++) {
            int idx = (ic * output_h * output_w) + (output_w * yh) + yw;
            float delta = dyPtr[idx];
            float max_val = -FLT_MAX;
            int max_idx = 0;
            for (int32_t kh = 0; kh < win_h; kh++) {
              int xh = yh * stride_h + kh - pad_h;
              if ((xh < 0) || (xh >= in_h)) {
                continue;
              }
              for (int32_t kw = 0; kw < win_w; kw++) {
                int xw = yw * stride_w + kw - pad_w;
                if ((xw < 0) || (xw >= in_w)) {
                  continue;
                }
                if (inPtr[(ic * in_h * in_w) + (xh * in_w) + xw] > max_val) {
                  max_val = inPtr[(ic * in_h * in_w) + (xh * in_w) + xw];
                  max_idx = (ic * in_h * in_w) + (xh * in_w) + xw;
                }
              }
            }
            out[max_idx] += delta;
          }
        }
      }
    }
  }
}
