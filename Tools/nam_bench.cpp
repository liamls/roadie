#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include "NAM/get_dsp.h"

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: nam_bench <path.nam> [bufferSize] [sampleRate]\n";
        return 1;
    }

    std::filesystem::path path (argv[1]);
    int bufferSize = argc > 2 ? std::atoi (argv[2]) : 128;
    double sampleRate = argc > 3 ? std::atof (argv[3]) : 48000.0;

    auto model = nam::get_dsp (path);
    model->Reset (sampleRate, bufferSize);

    std::vector<float> buf (static_cast<size_t> (bufferSize), 0.0f);

    for (int i = 0; i < 50; ++i)
    {
        float* in[1] = { buf.data() };
        float* out[1] = { buf.data() };
        model->process (in, out, bufferSize);
    }

    const int iterations = 2000;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i)
    {
        float* in[1] = { buf.data() };
        float* out[1] = { buf.data() };
        model->process (in, out, bufferSize);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalMs = std::chrono::duration<double, std::milli> (end - start).count();
    double perBlockMs = totalMs / iterations;
    double blockBudgetMs = (static_cast<double> (bufferSize) / sampleRate) * 1000.0;
    double loadPercent = (perBlockMs / blockBudgetMs) * 100.0;

    std::cout << "model: " << path.filename().string() << "\n"
              << "bufferSize=" << bufferSize << " sampleRate=" << sampleRate << "\n"
              << "avg per-block processing: " << perBlockMs << " ms\n"
              << "block budget (real-time): " << blockBudgetMs << " ms\n"
              << "single-block CPU load: " << loadPercent << " %\n"
              << ((loadPercent < 60.0) ? "=> comfortable margin\n"
                                        : (loadPercent < 90.0) ? "=> tight but workable\n"
                                                                : "=> too slow for this buffer size\n");

    return 0;
}
