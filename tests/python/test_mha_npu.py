#
# Copyright © 2024 Advanced Micro Devices, Inc. All rights reserved.
#

import math
import os

import numpy as np
import pytest
import torch

torch.random.manual_seed(123)

M = [
    #   5,
    #   78,
    #   128,
    #   140,
    #   256,
    #   293,
    #   372,
    #   512,
    #   580,
    #   717,
    #   790,
    #   800,
    #   900,
    #   1024,
    #   1050,
    #   1320,
    #   1537,
    #   1580,
    #   1670,
    #   1900,
    #   2048,
]


@pytest.mark.parametrize("M", M)
def test_ryzenai_torch_mha_npu(M):
    sizeA = [32, M, 128]
    sizeB = [32, M, 128]
    sizeC = [32, M, 128]
    sizeD = [1, M, M]
    xFloat = (np.random.rand(*sizeA) - 0.5) * 1.5
    yFloat = (np.random.rand(*sizeB) - 0.5) * 1.5
    zFloat = (np.random.rand(*sizeC) - 0.5) * 1.5
    pFloat = (np.random.rand(*sizeD) - 0.5) * 1.5

    query_states = torch.from_numpy(xFloat).to(torch.bfloat16)
    key_states = torch.from_numpy(yFloat).to(torch.bfloat16)
    value_states = torch.from_numpy(zFloat).to(torch.bfloat16)
    attention_mask = torch.from_numpy(pFloat).to(torch.bfloat16)

    def mha_local(query_states, key_states, value_states, attention_mask):
        res = torch.matmul(query_states, key_states.transpose(1, 2)) * (
            1 / math.sqrt(128)
        )
        res = res + attention_mask
        res = torch.nn.functional.softmax(res, dim=-1)
        res = torch.matmul(res, value_states)
        if os.getenv("MLADF_VERSION", "v1") == "v1":
            return res.transpose(0, 1)
        else:
            return res

    ref = mha_local(query_states, key_states, value_states, attention_mask)

    if os.environ.get("MLADF") and os.environ.get("DEVICE") == "stx":
        import ryzenai_torch_cpp

        op = ryzenai_torch_cpp.aie_mha_npu_torch()
        out = op.execute(query_states, key_states, value_states, attention_mask)
        if os.getenv("MLADF_VERSION", "v1") == "v1":
            out = out[:M, :, :]
        else:
            out = out[:, :M, :]
    else:
        print("Not implemented in NPU")
        out = mha_local(query_states, key_states, value_states, attention_mask)

    result = torch.allclose(ref, out, atol=0.9, rtol=0.5)
    print(f"ref: {ref[0]}")
    print(f"out: {out[0]}")
    print(f"err: {(ref-out).abs().max()}")
    if result:
        print(f"**** PASS: y vs x: {result}")
    else:
        print(f"**** FAIL: y vs x: {result}")

    assert result == True


def profile_mha():
    bmm2 = ryzenai_torch_cpp.aie_bmm_torch(False)
    x = torch.rand((32, 2048, 2048)).to(torch.bfloat16)
    y = torch.rand((32, 128, 2048)).to(torch.bfloat16)

    import time

    tarr = []
    for i in range(10):
        st = time.perf_counter()
        # res = bmm2.execute(x, y)
        res = torch.matmul(x, y.transpose(1, 2))
        res = res.unsqueeze(0)
        tarr.append(time.perf_counter() - st)

    tarr = np.array(tarr)
    print(f"bmm2 tarr:   {tarr}")
    print(f"bmm2 avg:    {tarr.mean()}")
    print(f"bmm2 median: {np.median(tarr)}")


if __name__ == "__main__":
    profile_mha()
