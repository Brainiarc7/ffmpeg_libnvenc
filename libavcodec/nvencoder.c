/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 */
#include "nvenc.h"
#include "nvencoder.h"
#include "nvencoder_utils.h"
#include <stdlib.h>

// Definitions
#if defined (_WIN32)
#define NVCUDA_LIB      TEXT("nvcuda.dll")
#if defined (_WIN64)
#define NVENCODEAPI_LIB TEXT("nvEncodeAPI64.dll")
#else
#define NVENCODEAPI_LIB TEXT("nvEncodeAPI.dll")
#endif
#else
#include <string.h>     // for memset
#include <dlfcn.h>
#define NVCUDA_LIB      "libcuda.so"
#define NVENCODEAPI_LIB "libnvidia-encode.so"
#define LoadLibrary(x)  dlopen(x, RTLD_NOW | RTLD_GLOBAL)
#define GetProcAddress  dlsym
#define FreeLibrary     dlclose
#endif

// Typedefs
#if defined (NV_CUDACTX)
typedef CUresult    (CUDAAPI  *cuinit_t)(unsigned int);
typedef CUresult    (CUDAAPI  *cudeviceget_t)(CUdevice*, int);
typedef CUresult    (CUDAAPI  *cudevicecomputecapability_t)(int*, int*, CUdevice);
typedef CUresult    (CUDAAPI  *cuctxcreate_t)(CUcontext*, unsigned int, CUdevice);
typedef CUresult    (CUDAAPI  *cuctxdestroy_t)(CUcontext);
#endif
#if defined (NV_D3DCTX)
typedef IDirect3D9* (WINAPI   *directd3dcreate9_t)(UINT);
#endif
typedef NVENCSTATUS (NVENCAPI *nvencodeapicreateinstance_t)(NV_ENCODE_API_FUNCTION_LIST*);

// Static data
static const GUID NV_ENC_CODEC_NULL_GUID =
{ 0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };

// Map numeric parameters to NVENCODEAPI definitions
static GUID map_profile(uint32_t profile)
{
    switch (profile)
    {
    case 66:
        return NV_ENC_H264_PROFILE_BASELINE_GUID;
    case 77:
        return NV_ENC_H264_PROFILE_MAIN_GUID;
    case 100:
        return NV_ENC_H264_PROFILE_HIGH_GUID;
    default:
        return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
    }
}
static NV_ENC_LEVEL map_level(uint32_t level)
{
    return (NV_ENC_LEVEL)(level);
}


static void deinit_device(nvencoder_t *nvenc)
{
#if defined (NV_CUDACTX)
    cuctxdestroy_t cuctxdestroy;

    if (nvenc->device.type == NV_ENC_DEVICE_TYPE_CUDA)
    {
        if (nvenc->device.cudacontext)
        {
            cuctxdestroy =
                (cuctxdestroy_t)GetProcAddress(nvenc->device.lib, "cuCtxDestroy");
            if (cuctxdestroy)
                cuctxdestroy((CUcontext)nvenc->device.cudacontext);
            nvenc->device.cudacontext = NULL;
        }
        if (nvenc->device.cudadevice)
        {
            nvenc->device.cudadevice = 0;
        }
        if (nvenc->device.lib)
        {
            FreeLibrary(nvenc->device.lib);
            nvenc->device.lib = NULL;
        }
    }
#endif // NV_CUDACTX
#if defined (NV_D3DCTX)
    if (nvenc->device.type == NV_ENC_DEVICE_TYPE_DIRECTX)
    {
        if (nvenc->device.d3ddevice)
        {
            IDirect3DDevice9_Release((IDirect3DDevice9*)nvenc->device.d3ddevice);
            nvenc->device.d3ddevice = NULL;
        }
        if (nvenc->device.d3d)
        {
            IDirect3D9_Release(nvenc->device.d3d);
            nvenc->device.d3d = NULL;
        }
        if (nvenc->device.lib)
        {
            FreeLibrary(nvenc->device.lib);
            nvenc->device.lib = NULL;
        }
    }
#endif // NV_D3DCTX

    nvenc->device.ptr = NULL;
    nvenc->device.type = 0;
}

static bool init_device(nvencoder_t *nvenc)
{
#if defined (NV_CUDACTX)
    CUresult cures;
    cuinit_t cunit;
    cudevicecomputecapability_t cudevicecomputecapability;
    cudeviceget_t cudeviceget;
    cuctxcreate_t cuctxcreate;
    int32_t sm_major, sm_minor;
#endif
#if defined (NV_D3DCTX)
    HRESULT hr;
    directd3dcreate9_t d3dcreate9;
    D3DPRESENT_PARAMETERS d3dpp;
#endif

#if defined (NV_CUDACTX)
    // Allocate CUDA context as basis
    nvenc->device.type = NV_ENC_DEVICE_TYPE_CUDA;
    nvenc->device.lib = LoadLibrary(NVCUDA_LIB);
    if (nvenc->device.lib)
    {
        cunit =
            (cuinit_t)GetProcAddress(nvenc->device.lib, "cuInit");
        cudevicecomputecapability =
            (cudevicecomputecapability_t)GetProcAddress(nvenc->device.lib, "cuDeviceComputeCapability");
        cudeviceget =
            (cudeviceget_t)GetProcAddress(nvenc->device.lib, "cuDeviceGet");
        cuctxcreate =
            (cuctxcreate_t)GetProcAddress(nvenc->device.lib, "cuCtxCreate");
        if (cunit && cudevicecomputecapability && cudeviceget && cuctxcreate)
        {
            cures = cunit(0);
            if (cures == CUDA_SUCCESS)
            {
                // Get a compatible, NVENC-present CUDA device
                cures = cudevicecomputecapability(&sm_major, &sm_minor, 0);
                if ((cures == CUDA_SUCCESS) && (sm_major >= 3))
                {
                    cures = cudeviceget(&nvenc->device.cudadevice, 0);
                    if (cures == CUDA_SUCCESS)
                    {
                        // Create the CUDA Context
                        cures = cuctxcreate(&nvenc->device.cudacontext, 0, nvenc->device.cudadevice);
                        if (cures == CUDA_SUCCESS)
                        {
                            nvenc->device.ptr = (void*)nvenc->device.cudacontext;
                            return true;
                        }
                    }
                }
            }
        }
    }
#endif // NV_CUDACTX

    deinit_device(nvenc);

#if defined (NV_D3DCTX)
    // Allocate D3D context as basis
    nvenc->device.type = NV_ENC_DEVICE_TYPE_DIRECTX;
    nvenc->device.lib = LoadLibrary(TEXT("d3d9.dll"));
    if (nvenc->device.lib)
    {
        d3dcreate9 =
            (directd3dcreate9_t)GetProcAddress(nvenc->device.lib, "Direct3DCreate9");
        if (d3dcreate9)
        {
            nvenc->device.d3d = d3dcreate9(D3D_SDK_VERSION);
            if (nvenc->device.d3d)
            {
                // Create the Direct3D9 device and the swap chain. In this example, the swap
                // chain is the same size as the current display mode. The format is RGB-32.
                memset(&d3dpp, 0, sizeof(d3dpp));
                d3dpp.Windowed             = TRUE;
                d3dpp.BackBufferFormat     = D3DFMT_UNKNOWN;
                d3dpp.BackBufferWidth      = 128;
                d3dpp.BackBufferHeight     = 128;
                d3dpp.BackBufferCount      = 0;
                d3dpp.SwapEffect           = D3DSWAPEFFECT_COPY;
                d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
                d3dpp.Flags                = D3DPRESENTFLAG_VIDEO;

                hr = IDirect3D9_CreateDevice(
                        nvenc->device.d3d,
                        D3DADAPTER_DEFAULT,
                        D3DDEVTYPE_HAL,
                        NULL,
                        D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING,
                        &d3dpp,
                        (IDirect3DDevice9**)&nvenc->device.d3ddevice);
                if (SUCCEEDED(hr))
                {
                    nvenc->device.ptr = (void*)nvenc->device.d3ddevice;
                    return true;
                }
            }
        }
    }
#endif // NV_D3DCTX

    return false;
}

static bool query_caps(nvencoder_t *nvenc)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;
    uint32_t count;
    uint32_t count_ret;

    // Enumerate codec GUIDs
    count = 0, count_ret = 0;
    nvenc_status = nvenc->api.nvEncGetEncodeGUIDCount(nvenc->inst, &count);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        nvenc->codec_guids = (GUID *)malloc(sizeof(GUID)* count);
        if (nvenc->codec_guids)
        {
            memset(nvenc->codec_guids, 0, sizeof(GUID)* count);
            nvenc_status = nvenc->api.nvEncGetEncodeGUIDs(nvenc->inst, nvenc->codec_guids, count, &count_ret);
            if ((nvenc_status != NV_ENC_SUCCESS) || (count_ret == 0))
            {
                return false;
            }
            nvenc->num_codec_guids = count_ret;
        }
    }

    // Enumerate codec profile GUIDs
    count = 0, count_ret = 0;
    nvenc_status = nvenc->api.nvEncGetEncodeProfileGUIDCount(nvenc->inst, NV_ENC_CODEC_H264_GUID, &count);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        nvenc->profile_guids = (GUID *)malloc(sizeof(GUID)* count);;
        if (nvenc->profile_guids)
        {
            memset(nvenc->profile_guids, 0, sizeof(GUID)* count);
            nvenc_status = nvenc->api.nvEncGetEncodeProfileGUIDs(nvenc->inst, NV_ENC_CODEC_H264_GUID, nvenc->profile_guids, count, &count_ret);
            if ((nvenc_status != NV_ENC_SUCCESS) || (count_ret == 0))
            {
                return false;
            }
            nvenc->num_preset_guids = count_ret;
        }
    }

    // Enumerate codec preset GUIDs
    count = 0, count_ret = 0;
    nvenc_status = nvenc->api.nvEncGetEncodePresetCount(nvenc->inst, NV_ENC_CODEC_H264_GUID, &count);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        nvenc->preset_guids = (GUID *)malloc(sizeof(GUID)* count);
        if (nvenc->preset_guids)
        {
            memset(nvenc->preset_guids, 0, sizeof(GUID)* count);
            nvenc_status = nvenc->api.nvEncGetEncodePresetGUIDs(nvenc->inst, NV_ENC_CODEC_H264_GUID, nvenc->preset_guids, count, &count_ret);
            if ((nvenc_status != NV_ENC_SUCCESS) || (count_ret == 0))
            {
                return false;
            }
            nvenc->num_profile_guids = count_ret;
        }
    }

    // Enumerate input formats
    count = 0, count_ret = 0;
    nvenc_status = nvenc->api.nvEncGetInputFormatCount(nvenc->inst, NV_ENC_CODEC_H264_GUID, &count);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        nvenc->buffer_fmts = (NV_ENC_BUFFER_FORMAT*)malloc(sizeof(NV_ENC_BUFFER_FORMAT)* count);
        if (nvenc->buffer_fmts)
        {
            memset(nvenc->buffer_fmts, 0, sizeof(NV_ENC_BUFFER_FORMAT)* count);
            nvenc_status = nvenc->api.nvEncGetInputFormats(nvenc->inst, NV_ENC_CODEC_H264_GUID, nvenc->buffer_fmts, count, &count_ret);
            if ((nvenc_status != NV_ENC_SUCCESS) || (count_ret == 0))
            {
                return false;
            }
            nvenc->num_buffer_fmts = count_ret;
        }
    }

    return true;
}

static bool open(nvencoder_t *nvenc)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;
    nvencodeapicreateinstance_t encodeapicreateinst;
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open_encode_session_params;

    // Dynamically load NVENC library
    nvenc->lib = LoadLibrary(NVENCODEAPI_LIB);

    if (!nvenc->lib)
    {
        return false;
    }
    encodeapicreateinst =
        (nvencodeapicreateinstance_t)GetProcAddress(nvenc->lib, "NvEncodeAPICreateInstance");
    if (!encodeapicreateinst)
    {
        return false;
    }

    // Initialize function table
    nvenc->api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    nvenc_status = encodeapicreateinst(&nvenc->api);
    if (nvenc_status != NV_ENC_SUCCESS)
    {
        return false;
    }

    if (!init_device(nvenc))
    {
        return false;
    }

    // Open encoder session
    memset(&open_encode_session_params, 0, sizeof(open_encode_session_params));
    open_encode_session_params.version      = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open_encode_session_params.apiVersion   = NVENCAPI_VERSION;
    open_encode_session_params.device       = nvenc->device.ptr;
    open_encode_session_params.deviceType   = nvenc->device.type;

    nvenc_status = nvenc->api.nvEncOpenEncodeSessionEx(&open_encode_session_params, &nvenc->inst);
    if (nvenc_status != NV_ENC_SUCCESS)
    {
        return false;
    }

    // Find encoder capabilities
    if (!query_caps(nvenc))
    {
        return false;
    }

    return true;
}

static bool allocate_io(nvencoder_t *nvenc)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;
    NV_ENC_CREATE_INPUT_BUFFER create_input_buffer;
    NV_ENC_CREATE_BITSTREAM_BUFFER create_bitstream_buffer;

    // Input buffer
    memset(&create_input_buffer, 0, sizeof(create_input_buffer));
    create_input_buffer.version    = NV_ENC_CREATE_INPUT_BUFFER_VER;
    create_input_buffer.width      = nvenc->init_params.maxEncodeWidth;
    create_input_buffer.height     = nvenc->init_params.maxEncodeHeight;
    create_input_buffer.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_UNCACHED;
    create_input_buffer.bufferFmt  = nvenc->buffer_fmt;

    nvenc_status = nvenc->api.nvEncCreateInputBuffer(nvenc->inst, &create_input_buffer);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        nvenc->i_buffer = create_input_buffer.inputBuffer;
        create_input_buffer.inputBuffer = NULL;
    }

    // Output buffer
    memset(&create_bitstream_buffer, 0, sizeof(create_bitstream_buffer));
    create_bitstream_buffer.version    = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    create_bitstream_buffer.size       = nvenc->init_params.maxEncodeWidth * nvenc->init_params.maxEncodeHeight;
    create_bitstream_buffer.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;

    nvenc_status = nvenc->api.nvEncCreateBitstreamBuffer(nvenc->inst, &create_bitstream_buffer);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        nvenc->o_buffer = create_bitstream_buffer.bitstreamBuffer;
        create_bitstream_buffer.bitstreamBuffer = NULL;
    }

    return true;
}

static void deallocate_io(nvencoder_t *nvenc)
{
    // Output buffer
    if (nvenc->o_buffer)
    {
        nvenc->api.nvEncDestroyBitstreamBuffer(nvenc->inst, nvenc->o_buffer);
        nvenc->o_buffer = NULL;
    }

    // Input buffer
    if (nvenc->i_buffer)
    {
        nvenc->api.nvEncDestroyInputBuffer(nvenc->inst, nvenc->i_buffer);
        nvenc->i_buffer = NULL;
    }
}

static void close(nvencoder_t *nvenc)
{
    if (nvenc->buffer_fmts)
    {
        free(nvenc->buffer_fmts);
        nvenc->buffer_fmts = NULL;
        nvenc->num_buffer_fmts = 0;
    }
    if (nvenc->preset_guids)
    {
        free(nvenc->preset_guids);
        nvenc->preset_guids = NULL;
        nvenc->num_codec_guids = 0;
    }
    if (nvenc->profile_guids)
    {
        free(nvenc->profile_guids);
        nvenc->profile_guids = NULL;
        nvenc->num_profile_guids = 0;
    }
    if (nvenc->codec_guids)
    {
        free(nvenc->codec_guids);
        nvenc->codec_guids = NULL;
        nvenc->num_codec_guids = 0;
    }

    if (nvenc->inst)
    {
        nvenc->api.nvEncDestroyEncoder(nvenc->inst);
        nvenc->inst = NULL;
    }

    deinit_device(nvenc);

    if (nvenc->lib)
    {
        FreeLibrary(nvenc->lib);
        nvenc->lib = NULL;
    }
}

static bool initialize(nvencoder_t *nvenc, nvenc_cfg_t *nvenc_cfg)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;

    //NV_ENC_CONFIG generic
    nvenc->config.version                   = NV_ENC_CONFIG_VER;
    nvenc->config.profileGUID               = map_profile(nvenc_cfg->profile);
    nvenc->config.gopLength                 = nvenc_cfg->gopLength;
    nvenc->config.frameIntervalP            = 1 + nvenc_cfg->numBFrames;
    nvenc->config.frameFieldMode            = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
    nvenc->config.mvPrecision               = NV_ENC_MV_PRECISION_QUARTER_PEL;

    //NV_ENC_CODEC_CONFIG rate-control
    nvenc->config.rcParams.version          = NV_ENC_RC_PARAMS_VER;
    nvenc->config.rcParams.rateControlMode  = (NV_ENC_PARAMS_RC_MODE)nvenc_cfg->rateControl;
    nvenc->config.rcParams.maxBitRate       = nvenc_cfg->peakBitRate;
    nvenc->config.rcParams.averageBitRate   = nvenc_cfg->avgBitRate;
    nvenc->config.rcParams.constQP.qpIntra  = nvenc_cfg->qpI;
    nvenc->config.rcParams.constQP.qpInterP = nvenc_cfg->qpP;
    nvenc->config.rcParams.constQP.qpInterB = nvenc_cfg->qpB;
    nvenc->config.rcParams.minQP.qpIntra    = nvenc_cfg->qpMin;
    nvenc->config.rcParams.minQP.qpInterP   = nvenc_cfg->qpMin;
    nvenc->config.rcParams.minQP.qpInterB   = nvenc_cfg->qpMin;
    nvenc->config.rcParams.maxQP.qpIntra    = nvenc_cfg->qpMax;
    nvenc->config.rcParams.maxQP.qpInterP   = nvenc_cfg->qpMax;
    nvenc->config.rcParams.maxQP.qpInterB   = nvenc_cfg->qpMax;

    //NV_ENC_CODEC_CONFIG codec
    nvenc->config.encodeCodecConfig.h264Config.outputAUD                  = nvenc_cfg->enableAUD;
    nvenc->config.encodeCodecConfig.h264Config.disableSPSPPS              = nvenc_cfg->disableSPSPPS;
    nvenc->config.encodeCodecConfig.h264Config.repeatSPSPPS               = nvenc_cfg->enableRepeatSPSPPS;
    nvenc->config.encodeCodecConfig.h264Config.level                      = map_level(nvenc_cfg->level);
    nvenc->config.encodeCodecConfig.h264Config.idrPeriod                  = nvenc_cfg->idrPeriod;
    nvenc->config.encodeCodecConfig.h264Config.fmoMode                    = NV_ENC_H264_FMO_DISABLE;
    nvenc->config.encodeCodecConfig.h264Config.maxNumRefFrames            = nvenc_cfg->numRefFrames;
    nvenc->config.encodeCodecConfig.h264Config.chromaFormatIDC            = 1;
    nvenc->config.encodeCodecConfig.h264Config.bdirectMode                = (NV_ENC_H264_BDIRECT_MODE)nvenc_cfg->bdirectMode;
    nvenc->config.encodeCodecConfig.h264Config.adaptiveTransformMode      = (NV_ENC_H264_ADAPTIVE_TRANSFORM_MODE)nvenc_cfg->adaptiveTransformMode;
    nvenc->config.encodeCodecConfig.h264Config.sliceMode                  = nvenc_cfg->sliceMode;
    nvenc->config.encodeCodecConfig.h264Config.sliceModeData              = nvenc_cfg->sliceModeData;
    nvenc->config.encodeCodecConfig.h264Config.outputBufferingPeriodSEI   = nvenc_cfg->enableSEIBufferPeriod;
    nvenc->config.encodeCodecConfig.h264Config.outputPictureTimingSEI     = nvenc_cfg->enableSEIPictureTime;
    nvenc->config.encodeCodecConfig.h264Config.disableDeblockingFilterIDC = nvenc_cfg->disableDeblockingFilterIDC;

    //NV_ENC_INIT_PARAMS
    nvenc->init_params.encodeConfig       = &nvenc->config;
    nvenc->init_params.version            = NV_ENC_INITIALIZE_PARAMS_VER;
    nvenc->init_params.encodeGUID         = NV_ENC_CODEC_H264_GUID;
    nvenc->init_params.presetGUID         = NV_ENC_PRESET_HQ_GUID;
    nvenc->init_params.encodeWidth        = nvenc_cfg->width;
    nvenc->init_params.encodeHeight       = nvenc_cfg->height;
    nvenc->init_params.darWidth           = nvenc_cfg->width;
    nvenc->init_params.darHeight          = nvenc_cfg->height;
    nvenc->init_params.frameRateNum       = nvenc_cfg->frameRateNum;
    nvenc->init_params.frameRateDen       = nvenc_cfg->frameRateDen;
    nvenc->init_params.enableEncodeAsync  = 0;
    nvenc->init_params.enablePTD          = 1;
    nvenc->init_params.maxEncodeWidth     = nvenc_cfg->width;
    nvenc->init_params.maxEncodeHeight    = nvenc_cfg->height;

    // Apply x264-style options that will override the above settings
    map_x264_params(&nvenc->init_params, nvenc_cfg->x264_paramc, nvenc_cfg->x264_paramv);

    nvenc_status = nvenc->api.nvEncInitializeEncoder(nvenc->inst, &nvenc->init_params);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        // Default to NV12 as preferred input format
        nvenc->buffer_fmt = NV_ENC_BUFFER_FORMAT_NV12_PL;

        return true;
    }

    return false;
}

static bool encode_frame(nvencoder_t *nvenc, nvenc_frame_t *nvenc_frame, bool *output, bool flush)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;
    NV_ENC_PIC_PARAMS pic_params;

    memset(&pic_params, 0, sizeof(pic_params));
    if (flush)
    {
        pic_params.version         = NV_ENC_PIC_PARAMS_VER;
        pic_params.encodePicFlags |= NV_ENC_PIC_FLAG_EOS;
    }
    else
    {
        pic_params.version         = NV_ENC_PIC_PARAMS_VER;
        pic_params.inputWidth      = nvenc_frame->width;
        pic_params.inputHeight     = nvenc_frame->height;
        pic_params.inputBuffer     = nvenc->i_buffer;
        pic_params.outputBitstream = nvenc->o_buffer;
        pic_params.bufferFmt       = nvenc->buffer_fmt;
        pic_params.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;
        pic_params.frameIdx        = nvenc_frame->frame_idx;
        if (nvenc_frame->force_idr)
            pic_params.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;
        if (nvenc_frame->force_intra)
            pic_params.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEINTRA;
    }

    nvenc_status = nvenc->api.nvEncEncodePicture(nvenc->inst, &pic_params);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        *output = true;
        return true;
    }
    if (nvenc_status == NV_ENC_ERR_NEED_MORE_INPUT)
    {
        *output = false;
        return true;
    }

    return false;
}

static bool feed_input(nvencoder_t *nvenc, uint8_t **planes, uint32_t *pitches, enum nvenc_pixfmt_t buffer_fmt)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;
    NV_ENC_LOCK_INPUT_BUFFER lock_input_buffer;
    uint8_t *src, *dst;
    uint32_t src_pitch, dst_pitch, x, y;

    memset(&lock_input_buffer, 0, sizeof(lock_input_buffer));
    lock_input_buffer.version     = NV_ENC_LOCK_BITSTREAM_VER;
    lock_input_buffer.inputBuffer = nvenc->i_buffer;

    nvenc_status = nvenc->api.nvEncLockInputBuffer(nvenc->inst, &lock_input_buffer);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        dst = (uint8_t*)lock_input_buffer.bufferDataPtr;
        dst_pitch = lock_input_buffer.pitch;

        if (buffer_fmt == NVENC_FMT_NV12)
        {
            // Y
            src = planes[0];
            src_pitch = pitches[0];
            for (y = 0; y < nvenc->init_params.encodeHeight; y++)
            {
                memcpy(dst, src, nvenc->init_params.encodeWidth);
                dst += dst_pitch;
                src += src_pitch;
            }
            // UV
            src = planes[1];
            src_pitch = pitches[1];
            for (y = 0; y < nvenc->init_params.encodeHeight / 2; y++)
            {
                memcpy(dst, src, nvenc->init_params.encodeWidth);
                dst += dst_pitch;
                src += src_pitch;
            }
        }
        else if (buffer_fmt == NVENC_FMT_YV12)
        {
            // Y
            src = planes[0];
            src_pitch = pitches[0];
            for (y = 0; y < nvenc->init_params.encodeHeight; y++)
            {
                memcpy(dst, src, nvenc->init_params.encodeWidth);
                dst += dst_pitch;
                src += src_pitch;
            }
            // UV interleaving
            for (y = 0; y < nvenc->init_params.encodeHeight / 2; y++)
            {
                for (x = 0; x < nvenc->init_params.encodeWidth; x += 2)
                {
                    dst[x]     = planes[1][(pitches[1] * y) + (x >> 1)];
                    dst[x + 1] = planes[2][(pitches[2] * y) + (x >> 1)];
                }
                dst += dst_pitch;
            }
        }

        nvenc->api.nvEncUnlockInputBuffer(nvenc->inst, nvenc->i_buffer);

        return true;
    }

    return false;
}

static bool fetch_output(nvencoder_t *nvenc, nvenc_bitstream_t *nvenc_bitstream)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;
    NV_ENC_LOCK_BITSTREAM lock_bitstream;
    uint8_t *src;
    uint32_t src_size;

    memset(&lock_bitstream, 0, sizeof(lock_bitstream));
    lock_bitstream.version         = NV_ENC_LOCK_BITSTREAM_VER;
    lock_bitstream.doNotWait       = 0;
    lock_bitstream.outputBitstream = nvenc->o_buffer;

    nvenc_status = nvenc->api.nvEncLockBitstream(nvenc->inst, &lock_bitstream);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        src = (uint8_t*)lock_bitstream.bitstreamBufferPtr;
        src_size = lock_bitstream.bitstreamSizeInBytes;

        // copy bitstream out
        if (nvenc_bitstream->payload &&
            nvenc_bitstream->payload_size >= src_size)
        {
            memcpy(nvenc_bitstream->payload, src, src_size);
            nvenc_bitstream->payload_size  = src_size;
            nvenc_bitstream->pic_idx  = lock_bitstream.frameIdx;
            nvenc_bitstream->pic_type = lock_bitstream.pictureType;
        }

        nvenc->api.nvEncUnlockBitstream(nvenc->inst, nvenc->o_buffer);

        return true;
    }

    return false;
}

static bool reconfig(nvencoder_t *nvenc, nvenc_cfg_t *nvenc_cfg)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;
    NV_ENC_RECONFIGURE_PARAMS reconfig_params;

    // Update initial encoder parameters that likely changed
    nvenc->init_params.encodeWidth  = nvenc_cfg->width;
    nvenc->init_params.encodeHeight = nvenc_cfg->height;
    nvenc->init_params.darWidth     = nvenc_cfg->width;
    nvenc->init_params.darWidth     = nvenc_cfg->height;
    nvenc->init_params.frameRateNum = nvenc_cfg->frameRateNum;
    nvenc->init_params.frameRateDen = nvenc_cfg->frameRateDen;
    nvenc->init_params.encodeConfig->rcParams.averageBitRate = nvenc_cfg->avgBitRate;
    nvenc->init_params.encodeConfig->rcParams.maxBitRate = nvenc_cfg->peakBitRate;

    // Update x264-style options that will override the above settings
    map_x264_params(&nvenc->init_params, nvenc_cfg->x264_paramc, nvenc_cfg->x264_paramv);

    memset(&reconfig_params, 0, sizeof(reconfig_params));
    reconfig_params.version      = NV_ENC_RECONFIGURE_PARAMS_VER;
    reconfig_params.resetEncoder = 1;
    memcpy(&reconfig_params.reInitEncodeParams, &nvenc->init_params, sizeof(nvenc->init_params));

    nvenc_status = nvenc->api.nvEncReconfigureEncoder(nvenc->inst, &reconfig_params);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        return true;
    }

    return false;
}

static bool get_header(nvencoder_t *nvenc, uint8_t **header, size_t *header_size)
{
    NVENCSTATUS nvenc_status = NV_ENC_ERR_GENERIC;
    NV_ENC_SEQUENCE_PARAM_PAYLOAD seq_param_payload;

    nvenc_status = nvenc->api.nvEncGetSequenceParams(nvenc->inst, &seq_param_payload);
    if (nvenc_status == NV_ENC_SUCCESS)
    {
        // copy header out
        if ((header && header_size) &&
            (*header_size >= (size_t)seq_param_payload.outSPSPPSPayloadSize))
        {
            *header_size = (size_t)seq_param_payload.outSPSPPSPayloadSize;
            memcpy(*header, seq_param_payload.spsppsBuffer, *header_size);
            return true;
        }
    }

    return false;
}


/**
 * Creates and initializes a new encode session.
 *
 * @param nvenc_cfg The encoder initialization parameters
 * @return The encoder instance, NULL on failure
 */
nvenc_t* nvenc_open(nvenc_cfg_t *nvenc_cfg)
{
    nvencoder_t *_nvenc = (nvencoder_t *)malloc(sizeof(nvencoder_t));
    if (_nvenc)
    {
        memset(_nvenc, 0, sizeof(nvencoder_t));

        if (open(_nvenc)                  &&
            initialize(_nvenc, nvenc_cfg) &&
            allocate_io(_nvenc))
        {
            return (nvenc_t*)_nvenc;
        }
        deallocate_io(_nvenc);
        close(_nvenc);
        free(_nvenc);
    }
    return NULL;
}

/**
 * Re-initializes an existing encode session with new parameters.
 * 
 * Only a subset of encoding parameters can be changed, which includes, not are
 * not necessarily limited to: dimensions, framerate, and bitrate.
 *
 * @param nvenc The encoder instance
 * @param nvenc_cfg The encoder initialization parameters
 * @return 0 on success, negative on failure
 */
int nvenc_reconfig(nvenc_t *nvenc, nvenc_cfg_t *nvenc_cfg)
{
    nvencoder_t *_nvenc = (nvencoder_t*)nvenc;
    if (_nvenc)
    {
        if (reconfig(_nvenc, nvenc_cfg))
        {
            return 0;
        }
    }
    return -1;
}

/**
 * Encodes a single picture with the given encode input and encode parameters
 *
 * @param nvenc The encoder instance
 * @param nvenc_frame The input data and config for the current picture
 * @param nvenc_bitstream The encoded output data
 * @return 0 on success, negative on failure, 1 on require more input
 */
int nvenc_encode(nvenc_t *nvenc, nvenc_frame_t *nvenc_frame, nvenc_bitstream_t *nvenc_bitstream)
{
    bool output;
    nvencoder_t *_nvenc = (nvencoder_t*)nvenc;
    if (_nvenc)
    {
        // Simple synchronous encoding
        if (feed_input(_nvenc, nvenc_frame->planes, nvenc_frame->stride, nvenc_frame->format) &&
            encode_frame(_nvenc, nvenc_frame, &output, false))
        {
            if (!output)
            {
                return 1;
            }
            if (fetch_output(_nvenc, nvenc_bitstream))
            {
                return 0;
            }
        }
    }

    return -1;
}

/**
* Retrieves the codec bitstream headers for the encode session.
*
* @param nvenc The encoder instance
* @param nvenc_header The buffer to hold the bitstream header
* @return 0 on success, negative on failure
*/
int nvenc_header(nvenc_t *nvenc, nvenc_header_t *nvenc_header)
{
    nvencoder_t *_nvenc = (nvencoder_t*)nvenc;
    if (_nvenc)
    {
        if (get_header(_nvenc, &nvenc_header->payload, &nvenc_header->payload_size))
        {
            return 0;
        }
    }
    return -1;
}

/**
* Closes the encode session and releases its resources.
*
* @param nvenc The encoder instance
*/
void nvenc_close(nvenc_t *nvenc)
{
    bool output;
    nvencoder_t *_nvenc = (nvencoder_t*)nvenc;
    if (_nvenc)
    {
        // Flush encoder
        encode_frame(_nvenc, NULL, &output, true);

        deallocate_io(_nvenc);
        close(_nvenc);
        free(_nvenc);
    }
}
