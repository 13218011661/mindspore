# Copyright 2019 Huawei Technologies Co., Ltd
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
import numpy as np

import mindspore as ms
import mindspore.nn as nn
from mindspore.common.api import _executor
from mindspore.ops import operations as P
from mindspore.ops import composite as C
from mindspore import Tensor, context
from tests.ut.python.ops.test_math_ops import VirtualLoss

class GradWrap(nn.Cell):
    def __init__(self, network):
        super(GradWrap, self).__init__()
        self.network = network

    def construct(self, x, y):
        return C.grad_all(self.network)(x, y)

class NetWithLoss(nn.Cell):
    def __init__(self, network):
        super(NetWithLoss, self).__init__()
        self.loss = VirtualLoss()
        self.network = network

    def construct(self, x, y):
        predict = self.network(x, y)
        return self.loss(predict)

class Net(nn.Cell):
    def __init__(self, shape, offset):
        super().__init__()
        self.index = Tensor(np.ones(shape), dtype=ms.int32)
        self.offset = offset
        self.elu = P.EmbeddingLookup()
        self.mm = P.BatchMatMul()

    def construct(self, x, y):
        out = self.elu(x, self.index, self.offset)
        out = self.mm(out, y)
        return out


def test_embeddinglookup_reducescatter_false():
    shape = [8, 8]
    offset = 8
    net = NetWithLoss(Net(shape, offset))
    net.set_auto_parallel()

    x = Tensor(np.ones([64, 32]), dtype=ms.float32)
    y = Tensor(np.ones([8, 32, 8]), dtype=ms.float32)
    _executor.compile(net, x, y)


def test_embeddinglookup_reducescatter_true():
    shape = [8, 8]
    offset = 8
    net = NetWithLoss(Net(shape, offset))
    net.set_auto_parallel()

    x = Tensor(np.ones([64, 32]), dtype=ms.float32)
    y = Tensor(np.ones([8, 32, 8]), dtype=ms.float32)
    _executor.compile(net, x, y)


def test_embeddinglookup_reducescatter_false_grad():
    shape = [8, 8]
    offset = 8
    net = GradWrap(NetWithLoss(Net(shape, offset)))
    net.set_auto_parallel()

    x = Tensor(np.ones([64, 32]), dtype=ms.float32)
    y = Tensor(np.ones([8, 32, 8]), dtype=ms.float32)
    _executor.compile(net, x, y)


def test_embeddinglookup_reducescatter_true_grad():
    context.set_context(save_graphs=True)
    shape = [8, 8]
    offset = 8
    net = GradWrap(NetWithLoss(Net(shape, offset)))
    net.set_auto_parallel()

    x = Tensor(np.ones([64, 32]), dtype=ms.float32)
    y = Tensor(np.ones([8, 32, 8]), dtype=ms.float32)
    _executor.compile(net, x, y)
