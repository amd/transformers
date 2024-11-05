#
# Copyright © 2023 Advanced Micro Devices, Inc. All rights reserved.
#

import gc
import json
import math
import sys
import time
from typing import Optional, Tuple

import ryzenai_torch_cpp
import torch

op = ryzenai_torch_cpp.aie_rmsnorm_torch()


class LlamaFastRMSNorm(torch.nn.Module):
    def __init__(self, hidden_size=4096, eps=1e-6):
        super().__init__()
        self.weight = torch.nn.Parameter(torch.ones(hidden_size))
        self.variance_epsilon = eps

    def forward(self, hidden_states, in_zerocpy=False, rettorch=True, in_len=0):
        if in_zerocpy == False:
            q_len = hidden_states.size()[1]

        else:
            q_len = in_len
        if q_len >= 128:
            if in_zerocpy:
                norm_out = op.execute(
                    hidden_states, self.weight.data, in_zerocpy, rettorch
                ).unsqueeze(0)
            else:
                norm_out = op.execute(
                    hidden_states[0].contiguous(),
                    self.weight.data,
                    in_zerocpy,
                    rettorch,
                ).unsqueeze(0)
            return norm_out
        else:
            hidden_states = hidden_states.to(torch.float32)
            variance = hidden_states.pow(2).mean(-1, keepdim=True)
            hidden_states = hidden_states * torch.rsqrt(
                variance + self.variance_epsilon
            )
            norm_out = self.weight * hidden_states.to(torch.bfloat16)
            return norm_out
