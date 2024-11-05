# LLMs on RyzenAI with llama.cpp

[llama-2-7b-chat-alias]: https://huggingface.co/TheBloke/Llama-2-7B-Chat-GGUF/blob/main/llama-2-7b-chat.Q4_0.gguf
[llama-3-8b-instruct-alias]: https://huggingface.co/QuantFactory/Meta-Llama-3-8B-Instruct-GGUF/blob/main/Meta-Llama-3-8B-Instruct.Q4_0.gguf
[qwen1_5-7b-chat-alias]: https://huggingface.co/Qwen/Qwen1.5-7B-Chat-GGUF/blob/main/qwen1_5-7b-chat-q4_0.gguf

## Example of running Llama3
Command:
```
bin\Release\main.exe -m ..\models\Meta-Llama-3-8b-Instruct.Q4_0.gguf -e -t 4 -n 400 -p "Building a website can be done in 10 simple steps:\nStep 1:"
```
Prompt:
```
Building a website can be done in 10 simple steps:\n
```
Output:
```
Step 1: Choose a Domain Name
Step 2: Select a Web Hosting Service
Step 3: Design Your Website
Step 4: Create the Content
Step 5: Build the Website
Step 6: Add Interactive Elements
Step 7: Optimize for Search Engines
Step 8: Test and Debug
Step 9: Launch and Promote
Step 10: Maintain and Update

Here's a more detailed overview of each step:

Step 1: Choose a Domain Name
Choose a unique and memorable domain name that reflects your brand or website's purpose. Make sure it's available and easy to spell.

Step 2: Select a Web Hosting Service
Choose a reliable web hosting service that meets your needs and budget. Consider factors like storage space, bandwidth, and customer support.

Step 3: Design Your Website
Create a design concept for your website that includes a layout, color scheme, and typography. You can hire a designer or use a website builder like Wix or Squarespace.

Step 4: Create the Content
Write and gather the content for your website, including text, images, and other media. Make sure it's engaging, informative, and easy to read.

Step 5: Build the Website
Use a website builder or coding skills to build the website structure, including the homepage, interior pages, and navigation. Make sure it's responsive and works on different devices.

Step 6: Add Interactive Elements
Add interactive elements like forms, buttons, and menus to enhance user engagement and conversion rates.

Step 7: Optimize for Search Engines
Optimize your website for search engines by using relevant keywords, meta tags, and header tags. This will improve your website's visibility and ranking.

Step 8: Test and Debug
Test your website for usability, functionality, and performance. Fix any bugs or issues that arise during testing.

Step 9: Launch and Promote
Launch your website and promote it through social media,
```

## Performance on PHX, HPT and STX

The following table provides the best token-time (tokens/sec) observed on PHX, HPT, and STX boards (4/2024)

| Model Name                                               | Quantization |  PHX  |  HPT  |  STX  |
|----------------------------------------------------------|--------------|-------|-------|-------|
| [llama-2-7b-chat][llama-2-7b-chat-alias]                 | Q4_0         | 2.9   | 5.5   | 7.6   |
| [llama-3-8b-instruct][llama-3-8b-instruct-alias]         | Q4_0         | 1.7   |       | 4.8   |
| [qwen1_5-7b-chat-alias][qwen1_5-7b-chat-alias]           | Q4_0         |       |       | 6.9   |

OS: Microsoft Windows 11 Pro 10.0.26085 Build 26085 and Driver: 11.201.8.138 3/29/2024 Versions are used.

There is 1 supported quantization scheme "Q4_0", as described in [quantization](./quantization.md).

For technical discussion of the RyzenAI backend in llama.cpp see [background](./background.md).

## Perplexity measurement (Model quality)
The perplexity example within Llama.cpp is used to measure perplexity over a given prompt (lower perplexity is better).

The perplexity measurements in table above are done against the wikitext2 test dataset (https://paperswithcode.com/dataset/wikitext-2), with context length of 512.

```console
cd %TRANSFORMERS_ROOT%\ext\llama.cpp\build\
bin\Release\main.exe -m ..\models\llama-2-7b-chat.Q4_0.gguf -f wikitext-2-raw\wiki.test.raw
```
Output:
```console
perplexity : calculating perplexity over 655 chunks
24.43 seconds per pass - ETA 4.45 hours
[1]4.3306,[2]4.8324,[3]5.4543,[4]6.0606 ...
```

The following table provides the perplexity measurement observed on PHX board

| Model Name                                               | Quantization |         PHX          |        CPU          |
|----------------------------------------------------------|--------------|----------------------|---------------------|
| [llama-2-7b-chat][llama-2-7b-chat-alias]                 | Q4_0         |  5.9627 +/- 0.03348  |  5.9628 +/- 0.03348 |
| [Phi-3-mini-4k-instruct][Phi-3-mini-4k-instruct]         | Q4_0         |  7.9821 +/- 0.05530  |  7.9795 +/- 0.05527 |

For more details of perplexity measurement on Llama.cpp refer to [perplexity](../../../ext/llama.cpp/README.md)

## Steps to run the models
Assumes Windows CMD shell

### Activate ryzenai-transformers conda-enviornment
```console
conda activate ryzenai-transformers
```

### Build and Install RyzenAI
```console
setup_phx.bat # or setup_stx.bat
set TRANSFORMERS_ROOT=%PYTORCH_AIE_PATH%

cd %TRANSFORMERS_ROOT%\ops\cpp
cmake -B build\ -DCMAKE_INSTALL_PREFIX=%CONDA_PREFIX%
cmake --build build\ --config=Release
cmake --install build\ --config=Release
```

### Build llama.cpp
```console
cd %TRANSFORMERS_ROOT%\ext\llama.cpp
cmake -B build\ -DCMAKE_PREFIX_PATH="%CONDA_PREFIX%;%XRT_PATH%" -DLLAMA_RYZENAI=ON
cmake --build build\ --config=Release
```

### Download desired model
Download the desired prequantized gguf model from huggingface.
Note: Must be Q4_0 quantized for offload to NPU
Example model: https://huggingface.co/TheBloke/Llama-2-7B-Chat-GGUF/blob/main/llama-2-7b-chat.Q4_0.gguf
Download the model to:
`%TRANSFORMERS_ROOT%\ext\llama.cpp\models`

### Run
```console
cd %TRANSFORMERS_ROOT%\ext\llama.cpp\build\
bin\Release\main.exe -m ..\models\llama-2-7b-chat.Q4_0.gguf -e -t 1 -n 400 -p "Building a website can be done in 10 simple steps:\nStep 1:"
```

## Profiling

The time-to-first-token and token-time calculation is described in the below figure.

![Token time](../../llm/figures/ttft_and_token_time_calc.png)
