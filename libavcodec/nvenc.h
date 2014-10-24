/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef _NVENC_H
#define _NVENC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * List of supported pixel formats
 */
enum nvenc_pixfmt_t
{
    NVENC_FMT_NV12,
    NVENC_FMT_YV12,
};

enum nvenc_pictype_t
{
    NVENC_PICTYPE_P,
    NVENC_PICTYPE_B,
    NVENC_PICTYPE_I,
    NVENC_PICTYPE_IDR,
};

/**
 * Handle to an encode session
 */
typedef struct nvenc_t nvenc_t;

/**
 * Initialization parameters for an encode session
 */
typedef struct nvenc_cfg_t
{
    // Basic
    uint32_t            width;
    uint32_t            height;
    uint32_t            frameRateNum;
    uint32_t            frameRateDen;

    // Codec
    uint32_t            profile;
    uint32_t            gopLength;
    uint32_t            numBFrames;
    uint32_t            numRefFrames;
    uint32_t            fieldMode;

    // Rate-control
    uint32_t            rateControl;
    uint32_t            avgBitRate;
    uint32_t            peakBitRate;
    uint32_t            qpI;
    uint32_t            qpP;
    uint32_t            qpB;
    uint32_t            qpMin;
    uint32_t            qpMax;
    uint32_t            vbvBufferSize;
    uint32_t            vbvInitialDelay;

    // H.264
    uint32_t            level;
    uint32_t            idrPeriod;
    bool                enableCABAC;
    bool                disableSPSPPS;
    bool                enableRepeatSPSPPS;
    bool                enableFMO;
    bool                enableAUD;
    bool                enableSEIBufferPeriod;
    bool                enableSEIPictureTime;
    bool                enableSEIUser;
    bool                enableVUI;
    uint32_t            intraRefreshCount;
    uint32_t            intraRefreshPeriod;
    uint32_t            bdirectMode;
    uint32_t            adaptiveTransformMode;
    uint32_t            sliceMode;
    uint32_t            sliceModeData;
    uint32_t            disableDeblockingFilterIDC;

    // x264-style list of options
    char              **x264_paramv;
    uint32_t            x264_paramc;
} nvenc_cfg_t;

/**
 * Configuration parameters for encoding a frame
 */
typedef struct nvenc_frame_t
{
    uint8_t            *planes[3];
    uint32_t            stride[3];

    uint32_t            width;
    uint32_t            height;
    enum nvenc_pixfmt_t format;
    uint32_t            frame_idx;
    uint32_t            frame_type;
    bool                force_idr;
    bool                force_intra;
} nvenc_frame_t;

/**
 * Encoded output bitstream data
 */
typedef struct nvenc_bitstream_t
{
    uint8_t            *payload;
    size_t              payload_size;

    uint32_t            pic_idx;
    enum nvenc_pictype_t pic_type;
} nvenc_bitstream_t;

/**
 * Bitstream header data
 */
typedef struct nvenc_header_t
{
    uint8_t            *payload;
    size_t              payload_size;
} nvenc_header_t;

nvenc_t*                nvenc_open(nvenc_cfg_t *nvenc_cfg);
int                     nvenc_encode(nvenc_t *nvenc, nvenc_frame_t *nvenc_frame, nvenc_bitstream_t *nvenc_bitstream);
void                    nvenc_close(nvenc_t *nvenc);
int                     nvenc_reconfig(nvenc_t *nvenc, nvenc_cfg_t *nvenc_cfg);
int                     nvenc_header(nvenc_t *nvenc, nvenc_header_t *nvenc_header);

#endif // _NVENC_H
