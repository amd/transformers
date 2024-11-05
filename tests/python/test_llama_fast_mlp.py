#
# Copyright © 2023 Advanced Micro Devices, Inc. All rights reserved.
#


import copy
import os
import pytest
import qlinear
import torch
import torch.nn.functional as F
from llama_fast_mlp_npu import LlamaFastMLP
from qmodule import WQLinear
from ryzenai_llm_engine import RyzenAILLMEngine

torch.manual_seed(0)

group_sizes = [128]  # , 64, 32]


class MLP(torch.nn.Module):
    def __init__(self, w_bit, group_size):
        super().__init__()
        self.gate_proj = WQLinear(
            w_bit=w_bit,
            group_size=group_size,
            in_features=4096,
            out_features=11008,
            bias=False,
            dev="cpu",
        )
        self.up_proj = WQLinear(
            w_bit=w_bit,
            group_size=group_size,
            in_features=4096,
            out_features=11008,
            bias=False,
            dev="cpu",
        )
        self.down_proj = WQLinear(
            w_bit=w_bit,
            group_size=group_size,
            in_features=11008,
            out_features=4096,
            bias=False,
            dev="cpu",
        )
        self.act_fn = torch.nn.SiLU(inplace=True)

    def forward(self, x):
        return self.down_proj(self.act_fn(self.gate_proj(x[0])) * self.up_proj(x[0]))


def pack(qw):
    import math

    qcompact = torch.empty(qw.shape[0], math.ceil(qw.shape[1] / 2), dtype=torch.uint8)
    j = 0
    for i in range(qw.shape[1]):
        if i % 2 == 0:
            qcompact[:, j] = qw[:, i]
            qcompact[:, j] = qcompact[:, j] << 4
        else:
            qcompact[:, j] = torch.bitwise_or(qcompact[:, j], qw[:, i])
            j += 1
    return qcompact


def prep_golden(l, h, w_bit, group_size):
    golden = MLP(w_bit, group_size)

    gshape = (11008, 4096)
    golden.gate_proj.qweight = torch.randint(l, h, size=gshape).to(torch.int8)
    golden.gate_proj.qzeros = torch.randint(
        l, h, size=(gshape[0], int(gshape[1] / group_size))
    ).to(torch.int8)
    golden.gate_proj.scales = torch.rand((gshape[0], int(gshape[1] / group_size))).to(
        torch.float32
    )

    gshape = (11008, 4096)
    golden.up_proj.qweight = torch.randint(l, h, size=gshape).to(torch.int8)
    golden.up_proj.qzeros = torch.randint(
        l, h, size=(gshape[0], int(gshape[1] / group_size))
    ).to(torch.int8)
    golden.up_proj.scales = torch.rand((gshape[0], int(gshape[1] / group_size))).to(
        torch.float32
    )

    gshape = (4096, 11008)
    golden.down_proj.qweight = torch.randint(l, h, size=gshape).to(torch.int8)
    golden.down_proj.qzeros = torch.randint(
        l, h, size=(gshape[0], int(gshape[1] / group_size))
    ).to(torch.int8)
    golden.down_proj.scales = torch.rand((gshape[0], int(gshape[1] / group_size))).to(
        torch.float32
    )
    optimized = LlamaFastMLP(precision="w4abf16")

    golden.gate_proj.qweight = pack(golden.gate_proj.qweight)
    golden.up_proj.qweight = pack(golden.up_proj.qweight)
    golden.down_proj.qweight = pack(golden.down_proj.qweight)

    optimized.gate_proj = copy.deepcopy(golden.gate_proj)
    optimized.up_proj = copy.deepcopy(golden.up_proj)
    optimized.down_proj = copy.deepcopy(golden.down_proj)

    optimized.act_fn = torch.nn.SiLU(inplace=True)

    print("\nGolden" + "*" * 20)
    print(golden)

    RyzenAILLMEngine.replace_node(
        golden,
        WQLinear,
        qlinear.QLinearPerGrp,
        (),
        {"device": "aie", "w_bit": w_bit, "group_size": group_size},
    )
    print("\n", golden)

    print("\nOptimized" + "*" * 20)
    print("\n", optimized)
    optimized.init_fastmlp(4096)
    print("\n", optimized)
    RyzenAILLMEngine.replace_node(
        optimized,
        WQLinear,
        qlinear.QLinearPerGrp,
        (),
        {"device": "aie", "w_bit": w_bit, "group_size": group_size},
    )

    for n, m in golden.named_modules():
        if isinstance(m, qlinear.QLinearPerGrp):
            print(f"Quantizing weights of golden : layer : {n}")
            m.device = "cpu"
            m.quantize_weights()

    for n, m in optimized.named_modules():
        if isinstance(m, qlinear.QLinearPerGrp):
            print(f"Quantizing weights of optimized : layer : {n}")
            m.device = "cpu"
            m.quantize_weights()

    for n, m in golden.named_modules():
        if isinstance(m, qlinear.QLinearPerGrp):
            print(f"Preparing weights of golden : layer : {n}")
            m.device = "aie"
            m.initialize_parameters()

    for n, m in optimized.named_modules():
        if isinstance(m, qlinear.QLinearPerGrp):
            print(f"Preparing weights of optimized : layer : {n}")
            m.device = "aie"
            m.initialize_parameters()

    print("\n", optimized)

    return golden, optimized


@pytest.mark.parametrize("group_size", group_sizes)
@pytest.mark.parametrize("w_bit", [4])
@pytest.mark.parametrize("m", [1, 128, 256, 512, 1024, 2048])
def test_llama_fast_mlp(w_bit, group_size, m):
    if os.getenv("MLADF") != "2x4x4":
        pytest.skip("Test only relevant for MLADF.")
    if w_bit == 3:
        l, h = -1, 1
    else:
        l, h = 0, 2

    golden, optimized = prep_golden(l, h, w_bit, group_size)
    print("*" * 20)
    x = torch.rand((1, m, 4096)).to(torch.bfloat16)

    y0 = golden.forward(x)
    shape_addr = [0, 0, m, 4096]
    y1 = optimized.forward(x, m)
    print(f"y0.shape : {y0.shape}")
    print(f"y1.shape : {y1.shape}")
    print(y0)
    print(y1)

    err = (y0 - y1).abs()
    print(f"err cpu vs npu: {err.min()} {err.max()}")

    assert torch.allclose(y0, y1, atol=8388609)
