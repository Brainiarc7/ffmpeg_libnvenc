/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef _NVENCODER_UTILS_H
#define _NVENCODER_UTILS_H

#include "nvEncodeAPI.h"
#include <stdbool.h>
#include <stdint.h>

bool map_x264_params(NV_ENC_INITIALIZE_PARAMS *nvenc_init_params, uint32_t argc, char *argv[]);

#endif //_NVENCODER_UTILS_H
