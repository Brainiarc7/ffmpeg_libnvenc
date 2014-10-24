/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef _NVENCODER_H
#define _NVENCODER_H

#include "nvEncodeAPI.h"
#include <stdbool.h>
#include <stdint.h>

#if !defined (_WIN32)
#define         NVENCAPI
typedef void*   HINSTANCE;
typedef void*   HANDLE;
#endif

// Allow either CUDA/D3D or both as context to encoding interface
#define NV_CUDACTX
#if defined (_WIN32)
#define NV_D3DCTX
#endif

#if defined (NV_D3DCTX)
#include <D3D9.h>
#endif
#if defined (NV_CUDACTX)
#include "cuda.h"
#endif

// Main encoding context
typedef struct nvencoder_t
{
    HINSTANCE                   lib;
    NV_ENCODE_API_FUNCTION_LIST api;

    HANDLE                      inst;
    GUID                       *codec_guids;
    GUID                       *profile_guids;
    GUID                       *preset_guids;
    NV_ENC_BUFFER_FORMAT       *buffer_fmts;
    uint32_t                    num_codec_guids;
    uint32_t                    num_profile_guids;
    uint32_t                    num_preset_guids;
    uint32_t                    num_buffer_fmts;

    NV_ENC_INITIALIZE_PARAMS    init_params;
    NV_ENC_CONFIG               config;
    NV_ENC_BUFFER_FORMAT        buffer_fmt;

    NV_ENC_INPUT_PTR            i_buffer;
    NV_ENC_OUTPUT_PTR           o_buffer;

    struct
    {
        HINSTANCE               lib;
#if defined (NV_D3DCTX)
        IDirect3D9             *d3d;
        IDirect3DDevice9       *d3ddevice;
#endif
#if defined (NV_CUDACTX)
        CUdevice                cudadevice;
        CUcontext               cudacontext;
#endif
        void                   *ptr;
        NV_ENC_DEVICE_TYPE      type;
    } device;
} nvencoder_t;

#endif // _NVENCODER_H
