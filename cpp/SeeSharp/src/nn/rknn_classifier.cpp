#include "nn/rknn_classifier.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#include <rknn_api.h>

RknnClassifier::~RknnClassifier()
{
    if (ctx_)
        rknn_destroy((rknn_context)ctx_);
}

bool RknnClassifier::init(const std::string& modelPath)
{
    FILE* f = fopen(modelPath.c_str(), "rb");
    if (!f)
    {
        fprintf(stderr, "rknn: can't open %s\n", modelPath.c_str());
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> model(size);
    if (fread(model.data(), 1, size, f) != (size_t)size)
    {
        fclose(f);
        return false;
    }
    fclose(f);

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, model.data(), size, 0);
    if (ret != RKNN_SUCC)
    {
        fprintf(stderr, "rknn_init failed: %d\n", ret);
        return false;
    }
    ctx_ = ctx;

    rknn_input_output_num io{};
    if (rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io)) == RKNN_SUCC)
        nOutputs_ = io.n_output;
    else
        nOutputs_ = 1;
    printf("rknn: %s loaded, %u output(s)\n", modelPath.c_str(), nOutputs_);
    return true;
}

std::vector<float> RknnClassifier::infer(const uint8_t* rgb, int bytes)
{
    std::vector<float> out;
    if (!ctx_)
        return out;

    rknn_input in{};
    in.index = 0;
    in.type = RKNN_TENSOR_UINT8;
    in.fmt = RKNN_TENSOR_NHWC;
    in.size = bytes;
    in.buf = (void*)rgb;
    if (rknn_inputs_set((rknn_context)ctx_, 1, &in) != RKNN_SUCC)
        return out;

    if (rknn_run((rknn_context)ctx_, nullptr) != RKNN_SUCC)
        return out;

    rknn_output o{};
    o.index = 0;
    o.want_float = 1;
    if (rknn_outputs_get((rknn_context)ctx_, 1, &o, nullptr) != RKNN_SUCC)
        return out;

    const float* p = (const float*)o.buf;
    out.assign(p, p + o.size / sizeof(float));
    rknn_outputs_release((rknn_context)ctx_, 1, &o);
    return out;
}
