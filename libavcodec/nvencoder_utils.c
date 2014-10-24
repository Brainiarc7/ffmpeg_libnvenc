/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 */
#include "nvencoder_utils.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

// List of all current x264 options
typedef struct x264_params_t
{
    // Presets
    char                           *profile;
    char                           *preset;
    char                           *tune;

    // Frame
    uint32_t                        keyint;
    uint32_t                        min_keyint;
    bool                            intra_refresh;
    uint32_t                        bframes;
    bool                            open_gop;
    bool                            no_cabac;
    uint32_t                        ref;
    bool                            no_deblock;
    uint32_t                        slices;

    // Rate control
    uint32_t                        qp;
    uint32_t                        bitrate;
    uint32_t                        vbv_maxrate;
    uint32_t                        vbv_bufsize;
    float                           vbv_init;
    uint32_t                        qpmin;
    uint32_t                        qpmax;
    float                           ipratio;
    float                           pbratio;

    // Input/Output
    char                           *output;
    char                           *input_fmt;
    char                           *input_csp;
    char                           *output_csp;
    uint32_t                        input_depth;
    char                           *input_range;
    char                           *fps;
    uint32_t                        seek;
    uint32_t                        frames;
    float                           level;
    bool                            aud;
} x264_params_t;

static bool guidcmp(const GUID *guid1, const GUID *guid2)
{
    return memcmp(guid1, guid2, sizeof(GUID)) == 0 ? true : false;
}

static GUID map_profile(const char *profile)
{
    if (!strncmp(profile, "baseline", 8))
        return NV_ENC_H264_PROFILE_BASELINE_GUID;
    if (!strncmp(profile, "main",     4))
        return NV_ENC_H264_PROFILE_MAIN_GUID;
    if (!strncmp(profile, "high",     4))
        return NV_ENC_H264_PROFILE_HIGH_GUID;

    return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
}
static GUID map_preset(const char *profile)
{
    // all the fast presets
    if (strstr(profile, "fast"))
        return NV_ENC_PRESET_HP_GUID;

    // all the slow presets
    if (strstr(profile, "slow"))
        return NV_ENC_PRESET_HQ_GUID;

    return NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID;
}
static GUID map_tune(const char *tune, const GUID *cur_preset)
{
    // quality
    if (!strncmp(tune, "psnr", 4) || !strncmp(tune, "ssim", 4))
        return NV_ENC_PRESET_HQ_GUID;

    // low-latency
    if (!strncmp(tune, "zerolatency", 11))
    {
        if (guidcmp(cur_preset, &NV_ENC_PRESET_HQ_GUID))
            return NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;
        else if (guidcmp(cur_preset, &NV_ENC_PRESET_HP_GUID))
            return NV_ENC_PRESET_LOW_LATENCY_HP_GUID;
        else
            return NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID;
    }

    return NV_ENC_PRESET_DEFAULT_GUID;
}

static bool parse_x264_params(x264_params_t *x264_params, char *x264_opt, char *x264_arg)
{
    if (!strcmp(x264_opt, "profile"))
        x264_params->profile = x264_arg;
    if (!strcmp(x264_opt, "preset"))
        x264_params->preset = x264_arg;

    if (!strcmp(x264_opt, "keyint"))
        x264_params->keyint = atoi(x264_arg);
    if (!strcmp(x264_opt, "min-keyint"))
        x264_params->min_keyint = atoi(x264_arg);
    if (!strcmp(x264_opt, "intra-refresh"))
        x264_params->intra_refresh = true;
    if (!strcmp(x264_opt, "bframes"))
        x264_params->bframes = atoi(x264_arg);
    if (!strcmp(x264_opt, "open-gop"))
        x264_params->open_gop = true;
    if (!strcmp(x264_opt, "no-cabac"))
        x264_params->no_cabac = true;
    if (!strcmp(x264_opt, "ref"))
        x264_params->ref = atoi(x264_arg);
    if (!strcmp(x264_opt, "no-deblock"))
        x264_params->no_deblock = true;
    if (!strcmp(x264_opt, "slices"))
        x264_params->slices = atoi(x264_arg);

    if (!strcmp(x264_opt, "qp"))
        x264_params->qp = atoi(x264_arg);
    if (!strcmp(x264_opt, "bitrate"))
        x264_params->bitrate = atoi(x264_arg);
    if (!strcmp(x264_opt, "vbv-maxrate"))
        x264_params->vbv_maxrate = atoi(x264_arg);
    if (!strcmp(x264_opt, "vbv-bufsize"))
        x264_params->vbv_bufsize = atoi(x264_arg);
    if (!strcmp(x264_opt, "vbv-init"))
        x264_params->vbv_init = (float)atof(x264_arg);
    if (!strcmp(x264_opt, "qpmin"))
        x264_params->qpmin = atoi(x264_arg);
    if (!strcmp(x264_opt, "qpmax"))
        x264_params->qpmax = atoi(x264_arg);
    if (!strcmp(x264_opt, "ipratio"))
        x264_params->ipratio = (float)atof(x264_arg);
    if (!strcmp(x264_opt, "pbratio"))
        x264_params->pbratio = (float)atof(x264_arg);

    if (!strcmp(x264_opt, "level"))
        x264_params->level = (float)atof(x264_arg);
    if (!strcmp(x264_opt, "aud"))
        x264_params->aud = true;

    return true;
}

static bool map_x264_to_nvenc(NV_ENC_INITIALIZE_PARAMS *nvenc_init_params, const x264_params_t *x264_params)
{
    uint32_t qp_ipdiff = (uint32_t)(6 * log2f(x264_params->ipratio > 0.f ? x264_params->ipratio : 1.4f));
    uint32_t qp_pbdiff = (uint32_t)(6 * log2f(x264_params->pbratio > 0.f ? x264_params->pbratio : 1.3f));

    // Preset
    if (x264_params->profile)
    {
        nvenc_init_params->encodeConfig->profileGUID = map_profile(x264_params->profile);
    }
    if (x264_params->preset)
    {
        nvenc_init_params->presetGUID = map_preset(x264_params->preset);
    }
    if (x264_params->tune)
    {
        nvenc_init_params->presetGUID = map_tune(x264_params->tune, &nvenc_init_params->presetGUID);
    }

    // Frame
    if ((x264_params->keyint > 0) || (x264_params->min_keyint > 0))
    {
        if ((x264_params->keyint == 0) || (x264_params->keyint > x264_params->min_keyint))
            nvenc_init_params->encodeConfig->gopLength = x264_params->min_keyint;
        else
            nvenc_init_params->encodeConfig->gopLength = x264_params->keyint;
        fprintf(stdout, "%s=%u\n", "gopLength", nvenc_init_params->encodeConfig->gopLength);
    }
    if (x264_params->intra_refresh)
    {
        const uint32_t fps = nvenc_init_params->frameRateNum / nvenc_init_params->frameRateDen;
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.enableIntraRefresh = 1;
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.intraRefreshCnt    = fps;
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.intraRefreshPeriod = fps;
        fprintf(stdout, "%s=%u\n", "enableIntraRefresh", fps);
    }
    if (x264_params->bframes > 0)
    {
        nvenc_init_params->encodeConfig->frameIntervalP = 0 + 1 + x264_params->bframes;
        fprintf(stdout, "%s=%u\n", "frameIntervalP",
            nvenc_init_params->encodeConfig->frameIntervalP);
    }
    if (x264_params->open_gop)
    {
        nvenc_init_params->encodeConfig->gopLength = NVENC_INFINITE_GOPLENGTH;
        fprintf(stdout, "%s=%s\n", "gopLength", "NVENC_INFINITE_GOPLENGTH");
    }
    if (x264_params->no_cabac)
    {
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.entropyCodingMode = NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC;
        fprintf(stdout, "%s=%s\n", "entropyCodingMode", "NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC");
    }
    if (x264_params->ref > 0)
    {
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.maxNumRefFrames = x264_params->ref;
        fprintf(stdout, "%s=%u\n", "maxNumRefFrames",
            nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.maxNumRefFrames);
    }
    if (x264_params->no_deblock > 0)
    {
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.disableDeblockingFilterIDC = x264_params->no_deblock;
        fprintf(stdout, "%s=%x\n", "disableDeblockingFilterIDC",
            nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.disableDeblockingFilterIDC);
    }
    if (x264_params->slices > 0)
    {
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.sliceMode = 3;
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.sliceModeData = x264_params->slices;
        fprintf(stdout, "%s=%u,%s=%u\n",
            "sliceMode", nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.sliceMode,
            "sliceModeData", nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.maxNumRefFrames);
    }

    // Rate control
    if (x264_params->qp > 0)
    {
        nvenc_init_params->encodeConfig->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
        nvenc_init_params->encodeConfig->rcParams.constQP.qpInterP = x264_params->qp;
        nvenc_init_params->encodeConfig->rcParams.constQP.qpInterB = x264_params->qp + qp_pbdiff;
        nvenc_init_params->encodeConfig->rcParams.constQP.qpIntra  = x264_params->qp - qp_ipdiff;
        fprintf(stdout, "%s=%u,%s=%u,%s=%u\n",
            "constQP.qpInterP", nvenc_init_params->encodeConfig->rcParams.constQP.qpInterP,
            "constQP.qpInterB", nvenc_init_params->encodeConfig->rcParams.constQP.qpInterB,
            "constQP.qpIntra" , nvenc_init_params->encodeConfig->rcParams.constQP.qpIntra);
    }
    if (x264_params->bitrate > 0)
    {
        nvenc_init_params->encodeConfig->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        nvenc_init_params->encodeConfig->rcParams.averageBitRate = x264_params->bitrate;
        fprintf(stdout, "%s=%u\n", "averageBitRate",
            nvenc_init_params->encodeConfig->rcParams.averageBitRate);
    }
    if (x264_params->vbv_maxrate > 0)
    {
        nvenc_init_params->encodeConfig->rcParams.maxBitRate = x264_params->vbv_maxrate;
        fprintf(stdout, "%s=%u\n", "maxBitRate",
            nvenc_init_params->encodeConfig->rcParams.maxBitRate);
    }
    if (x264_params->vbv_bufsize > 0)
    {
        nvenc_init_params->encodeConfig->rcParams.vbvBufferSize = x264_params->vbv_bufsize;
        fprintf(stdout, "%s=%u\n", "vbvBufferSize",
            nvenc_init_params->encodeConfig->rcParams.vbvBufferSize);
    }
    if (x264_params->vbv_init > 0.f)
    {
        nvenc_init_params->encodeConfig->rcParams.vbvInitialDelay = (uint32_t)x264_params->vbv_init;
        fprintf(stdout, "%s=%u\n", "vbvInitialDelay",
            nvenc_init_params->encodeConfig->rcParams.vbvInitialDelay);
    }
    if (x264_params->qpmin > 0.f)
    {
        nvenc_init_params->encodeConfig->rcParams.enableMinQP = 1;
        nvenc_init_params->encodeConfig->rcParams.minQP.qpInterP = x264_params->qpmin;
        nvenc_init_params->encodeConfig->rcParams.minQP.qpInterB = x264_params->qpmin;
        nvenc_init_params->encodeConfig->rcParams.minQP.qpIntra  = x264_params->qpmin;
        fprintf(stdout, "%s=%u,%s=%u,%s=%u\n",
            "minQP.qpInterP", nvenc_init_params->encodeConfig->rcParams.minQP.qpInterP,
            "minQP.qpInterB", nvenc_init_params->encodeConfig->rcParams.minQP.qpInterB,
            "minQP.qpIntra" , nvenc_init_params->encodeConfig->rcParams.minQP.qpIntra);
    }
    if (x264_params->qpmax > 0.f)
    {
        nvenc_init_params->encodeConfig->rcParams.enableMaxQP = 1;
        nvenc_init_params->encodeConfig->rcParams.maxQP.qpInterP = x264_params->qpmax;
        nvenc_init_params->encodeConfig->rcParams.maxQP.qpInterB = x264_params->qpmax;
        nvenc_init_params->encodeConfig->rcParams.maxQP.qpIntra  = x264_params->qpmax;
        fprintf(stdout, "%s=%u,%s=%u,%s=%u\n",
            "maxQP.qpInterP", nvenc_init_params->encodeConfig->rcParams.maxQP.qpInterP,
            "maxQP.qpInterB", nvenc_init_params->encodeConfig->rcParams.maxQP.qpInterB,
            "maxQP.qpIntra" , nvenc_init_params->encodeConfig->rcParams.maxQP.qpIntra);
    }

    if (x264_params->level > 0.f)
    {
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.level =
            (uint32_t)(x264_params->level * 10.f);
        fprintf(stdout, "%s=%u\n", "level",
            nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.level);
    }
    if (x264_params->aud)
    {
        nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.outputAUD = x264_params->aud;
        fprintf(stdout, "%s=%u\n", "aud",
            nvenc_init_params->encodeConfig->encodeCodecConfig.h264Config.outputAUD);
    }

    return false;
}

bool map_x264_params(NV_ENC_INITIALIZE_PARAMS *nvenc_init_params, uint32_t argc, char *argv[])
{
    x264_params_t *x264_params;
    uint32_t i;

    x264_params = malloc(sizeof(x264_params_t));
    if (x264_params)
    {
        memset(x264_params, 0, sizeof(x264_params_t));

        // First, parse all understandable options (that appear in any order)
        for (i = 0; i < argc; i += 2)
        {
            parse_x264_params(x264_params, argv[i], argv[i + 1]);
        }
        // Second, map and apply the x264 options to nvenc all at once
        map_x264_to_nvenc(nvenc_init_params, x264_params);

        free(x264_params);

        return true;
    }

    return false;
}
