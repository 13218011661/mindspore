# Copyright 2020 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

import pytest
import numpy as np
from mindspore import Tensor
from mindspore.ops import operations as P
import mindspore.nn as nn
import mindspore.context as context

class TensorAdd(nn.Cell):
    def __init__(self):
        super(TensorAdd, self).__init__()
        self.add = P.TensorAdd()

    def construct(self, x, y):
        res = self.add(x, y)
        return res


@pytest.mark.level0
@pytest.mark.platform_x86_cpu
@pytest.mark.env_onecard
def test_tensor_add():
    x = np.arange(1 * 3 * 3 * 3).reshape(1, 3, 3, 3).astype(np.float32)
    y = np.arange(1 * 3 * 3 * 3).reshape(1, 3, 3, 3).astype(np.float32)

    context.set_context(mode=context.GRAPH_MODE, device_target='CPU')
    add = TensorAdd()
    output = add(Tensor(x), Tensor(y))
    assert (output.asnumpy() == x + y).all()
