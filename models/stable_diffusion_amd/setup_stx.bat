SET DEVICE=stx
set XLNX_ENABLE_CACHE=0

@REM Use CPU runner
set USE_CPU_RUNNER=0
set VAIP_COMPILE_RESERVE_CONST_DATA=0

@REM Enable Verbosity
set XLNX_ONNX_EP_VERBOSE=0
set XLNX_ENABLE_DUMP_XIR_MODEL=0
set XLNX_ENABLE_DUMP_ONNX_MODEL=0
set ENABLE_SAVE_ONNX_MODEL=0
set NUM_OF_DPU_RUNNERS=1

set DEBUG_DPU_CUSTOM_OP=0
set DEBUG_GEMM_CUSTOM_OP=0
set DEBUG_GRAPH_RUNNER=0

@REM Enable Multi-DPU flow
set XLNX_USE_SHARED_CONTEXT=0

@REM Enable DPU profiling
set DEEPHI_PROFILING=0

@REM Repo paths
SET PWD=%~dp0
set XLNX_VART_FIRMWARE=%PWD%\xclbin/%DEVICE%/AMD_AIE2P_4x4_Overlay_CFG0.xclbin
@REM set XLNX_VART_SKIP_FP_CHK=TRUE
set XLNX_TARGET_NAME=AMD_AIE2P_4x4_Overlay_CFG0


set EP=vai
set RUNNER=dpu
set ITERS=1

@REM Disable Matmul and Concat in CONV DPU
@REM set DISABLE_MATMUL_DPU_SG=0
@REM set DISABLE_CONCAT_DPU_SG=0
@REM set DISABLE_GEMM_DPU_SG=1
set VAIP_ALIGNMENT_TO_ORT_SKIP_CONCAT=1
set VAIP_ALIGNMENT_TO_ORT_SKIP_RESIZE=1
set VAIP_SKIP_MATMUL_DIMS_NOT_EQUAL_4=1
set VAIP_SKIP_SLICE_DIMS_NOT_EQUAL_4=1