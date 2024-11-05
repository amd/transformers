#ifndef __LINEAR_TORCH__
#define __LINEAR_TORCH__
#include <torch/extension.h>

namespace cpu {
class linear {
public:
  torch::Tensor mmul(torch::Tensor x, torch::Tensor weights);
};
} // namespace cpu
#endif
