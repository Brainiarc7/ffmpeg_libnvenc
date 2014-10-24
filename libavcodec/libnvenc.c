#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "nvenc.h"
#include <float.h>

// ffmpeg-x264 param-to-string macros
#define OPT_STRSTR(x, y)\
    if (y)\
    {\
        x264_argv[x264_argc++] = av_strdup(x);  \
        x264_argv[x264_argc++] = av_strdup(y);  \
    }
#define OPT_NUMSTR(x, y)\
    if (y > 0)\
    {\
        x264_argv[x264_argc++] = av_strdup(x);               \
        x264_argv[x264_argc] = av_malloc(sizeof(char) * 32); \
        sprintf(x264_argv[x264_argc++], "%u", y);            \
    }
#define OPT_BOOLSTR(x, y)\
    if (y)\
    {\
        x264_argv[x264_argc++] = av_strdup(x);  \
        x264_argv[x264_argc++] = av_strdup("1");\
    }

typedef struct NvEncContext {
    const AVClass  *class;

    nvenc_t        *nvenc;          // NVENC encoder instance
    nvenc_cfg_t     nvenc_cfg;      // NVENC encoder config

    char           *x264_opts;      // List of x264 options in opt:arg or opt=arg format
    char           *x264_params;    // List of x264 options in opt:arg or opt=arg format

    char           *preset;
    char           *tune;
    char           *profile;
    char           *level;
    int             fastfirstpass;
    char           *wpredp;
    float           crf;
    float           crf_max;
    int             cqp;
    int             aq_mode;
    float           aq_strength;
    char           *psy_rd;
    int             psy;
    int             rc_lookahead;
    int             weightp;
    int             weightb;
    int             ssim;
    int             intra_refresh;
    int             bluray_compat;
    int             b_bias;
    int             b_pyramid;
    int             mixed_refs;
    int             dct8x8;
    int             fast_pskip;
    int             aud;
    int             mbtree;
    char           *deblock;
    float           cplxblur;
    char           *partitions;
    int             direct_pred;
    int             slice_max_size;
    char           *stats;
    int             nal_hrd;
} NvEncContext;

static const enum AVPixelFormat nvenc_pix_fmts[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_NONE
};
static int map_avpixfmt_bufferformat(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return NVENC_FMT_YV12;
    case AV_PIX_FMT_NV12:
        return NVENC_FMT_NV12;
    default:
        return 0;
    }
}
static int map_avpixfmt_numplanes(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return 3;
    case AV_PIX_FMT_NV12:
        return 2;
    default:
        return 3;
    }
}

static av_cold int ff_libnvenc_init(AVCodecContext *avctx)
{
    NvEncContext *nvenc_ctx = (NvEncContext*)avctx->priv_data;
    int x264_argc;
    char **x264_argv;

    // Basic
    nvenc_ctx->nvenc_cfg.width        = avctx->width;
    nvenc_ctx->nvenc_cfg.height       = avctx->height;
    nvenc_ctx->nvenc_cfg.frameRateNum = avctx->time_base.den;
    nvenc_ctx->nvenc_cfg.frameRateDen = avctx->time_base.num * avctx->ticks_per_frame;

    // Codec
    if (avctx->profile >= 0)
        nvenc_ctx->nvenc_cfg.profile      = avctx->profile;
    if (avctx->gop_size >= 0)
        nvenc_ctx->nvenc_cfg.gopLength    = avctx->gop_size;
    else if (!(avctx->flags & CODEC_FLAG_CLOSED_GOP))
        nvenc_ctx->nvenc_cfg.gopLength    = UINT_MAX;   // infinite GOP
    if (avctx->max_b_frames >= 0)
        nvenc_ctx->nvenc_cfg.numBFrames   = avctx->max_b_frames;
    if (avctx->refs >= 0)
        nvenc_ctx->nvenc_cfg.numRefFrames = avctx->refs;
    if (avctx->flags & CODEC_FLAG_INTERLACED_DCT)
        nvenc_ctx->nvenc_cfg.fieldMode    = 2;

    // Rate-control
    if (avctx->bit_rate > 0) {
        nvenc_ctx->nvenc_cfg.rateControl   = 2;
        nvenc_ctx->nvenc_cfg.avgBitRate    = avctx->bit_rate;
    }
    if (avctx->rc_max_rate >= 0) {
        nvenc_ctx->nvenc_cfg.rateControl   = 1;
        nvenc_ctx->nvenc_cfg.peakBitRate   = avctx->rc_max_rate;
    }
    if (avctx->qmin >= 0)
        nvenc_ctx->nvenc_cfg.qpMin         = avctx->qmin;
    if (avctx->qmax >= 0)
        nvenc_ctx->nvenc_cfg.qpMax         = avctx->qmax;
    if (avctx->rc_buffer_size > 0) {
        nvenc_ctx->nvenc_cfg.vbvBufferSize = avctx->rc_buffer_size;
        if (avctx->rc_initial_buffer_occupancy >= 0) {
            nvenc_ctx->nvenc_cfg.vbvInitialDelay =
                avctx->rc_initial_buffer_occupancy / avctx->rc_buffer_size;
        }
    }

    // Codec-specific
    if (avctx->level >= 0)
        nvenc_ctx->nvenc_cfg.level     = avctx->level;
    if (avctx->gop_size >= 0)
        nvenc_ctx->nvenc_cfg.idrPeriod = avctx->gop_size;
    if (avctx->slices > 0) {
        nvenc_ctx->nvenc_cfg.sliceMode = 3;
        nvenc_ctx->nvenc_cfg.sliceModeData = avctx->slices;
    }
    else if (avctx->rtp_payload_size > 0) {
        nvenc_ctx->nvenc_cfg.sliceMode = 1;
        nvenc_ctx->nvenc_cfg.sliceModeData = avctx->rtp_payload_size;
    }
    if (avctx->coder_type == FF_CODER_TYPE_AC)
        nvenc_ctx->nvenc_cfg.enableCABAC = 1;
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER)
        nvenc_ctx->nvenc_cfg.enableRepeatSPSPPS = 1;

    // Allocate list of x264 options
    x264_argc = 0;
    x264_argv = av_calloc(255, sizeof(char*));
    if (!x264_argv)
        return -1;

    // ffmpeg-x264 parameters
    OPT_STRSTR("preset", nvenc_ctx->preset);
    OPT_STRSTR("tune", nvenc_ctx->tune);
    OPT_STRSTR("profile", nvenc_ctx->profile);
    OPT_STRSTR("level", nvenc_ctx->level);
    OPT_NUMSTR("qp", nvenc_ctx->cqp);
    OPT_NUMSTR("intra-refresh", nvenc_ctx->intra_refresh);
    OPT_NUMSTR("aud", nvenc_ctx->aud);
    OPT_STRSTR("deblock", nvenc_ctx->deblock);
    OPT_NUMSTR("direct-pred", nvenc_ctx->direct_pred);
    OPT_NUMSTR("nal_hrd", nvenc_ctx->nal_hrd);
    OPT_NUMSTR("8x8dct", nvenc_ctx->dct8x8);

    // x264-style extra parameters
    if (nvenc_ctx->x264_params) {
        AVDictionary *param_dict = NULL;
        AVDictionaryEntry *param_entry = NULL;

        if (!av_dict_parse_string(&param_dict, nvenc_ctx->x264_params, "=", ":", 0)) {
            while ((param_entry = av_dict_get(param_dict, "", param_entry, AV_DICT_IGNORE_SUFFIX))) {
                x264_argv[x264_argc++] = av_strdup(param_entry->key);
                x264_argv[x264_argc++] = av_strdup(param_entry->value);
            }
            av_dict_free(&param_dict);
        }
    }
    // x264-style extra options
    if (nvenc_ctx->x264_opts) {
        AVDictionary *param_dict = NULL;
        AVDictionaryEntry *param_entry = NULL;

        if (!av_dict_parse_string(&param_dict, nvenc_ctx->x264_opts, "=", ":", 0)) {
            while ((param_entry = av_dict_get(param_dict, "", param_entry, AV_DICT_IGNORE_SUFFIX))) {
                x264_argv[x264_argc++] = av_strdup(param_entry->key);
                x264_argv[x264_argc++] = av_strdup(param_entry->value);
            }
            av_dict_free(&param_dict);
        }
    }

    // Notify encoder to use the list of x264 options
    nvenc_ctx->nvenc_cfg.x264_paramc = x264_argc;
    nvenc_ctx->nvenc_cfg.x264_paramv = x264_argv;

    // Create and initialize nvencoder
    nvenc_ctx->nvenc = nvenc_open(&nvenc_ctx->nvenc_cfg);
    if (!nvenc_ctx->nvenc)
        return -1;

    avctx->coded_frame = av_frame_alloc();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    avctx->has_b_frames = (nvenc_ctx->nvenc_cfg.numBFrames > 0) ? 1 : 0;
    if (avctx->max_b_frames < 0)
        avctx->max_b_frames = 0;
    avctx->bit_rate = nvenc_ctx->nvenc_cfg.avgBitRate;

    return 0;
}

static int ff_libnvenc_encode(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
    NvEncContext *nvenc_ctx = (NvEncContext*)avctx->priv_data;

    int ret, i, user_packet = !!pkt->data;
    nvenc_frame_t nvenc_frame;
    nvenc_bitstream_t nvenc_bitstream;

    // Check for sudden change in parameters
    if ((avctx->width != nvenc_ctx->nvenc_cfg.width) ||
        (avctx->height != nvenc_ctx->nvenc_cfg.height)) {
        ret = nvenc_reconfig(nvenc_ctx->nvenc, &nvenc_ctx->nvenc_cfg);
        if (ret < 0)
            return -1;
    }

    // Setup input
    memset(&nvenc_frame, 0, sizeof(nvenc_frame));
    if (frame) {
        for (i = 0; i < map_avpixfmt_numplanes(avctx->pix_fmt); i++) {
            nvenc_frame.planes[i] = frame->data[i];
            nvenc_frame.stride[i] = frame->linesize[i];
        }
        nvenc_frame.width  = avctx->width;
        nvenc_frame.height = avctx->height;
        nvenc_frame.format = map_avpixfmt_bufferformat(avctx->pix_fmt);
    }

    // Setup output
    ret = ff_alloc_packet2(avctx, pkt, nvenc_frame.width * nvenc_frame.height);
    if (ret < 0)
        return -1;
    memset(&nvenc_bitstream, 0, sizeof(nvenc_bitstream));
    nvenc_bitstream.payload      = pkt->data;
    nvenc_bitstream.payload_size = pkt->size;

    // Encode the picture
    ret = nvenc_encode(nvenc_ctx->nvenc, &nvenc_frame, &nvenc_bitstream);

    if (ret < 0) {
        // Encoding failed
        if (!user_packet)
            av_free_packet(pkt);
        return -1;
    } else if (ret > 0) {
        // Encoding needs more input to produce output
        pkt->size = 0;
        *got_packet = 0;
        return 0;
    } else {
        // Encoding succeeded
        pkt->size   = nvenc_bitstream.payload_size;
        pkt->flags |= nvenc_bitstream.pic_type == NVENC_PICTYPE_IDR ? AV_PKT_FLAG_KEY : 0;

        avctx->coded_frame->pict_type =
            nvenc_bitstream.pic_type == NVENC_PICTYPE_P ? AV_PICTURE_TYPE_P :
            nvenc_bitstream.pic_type == NVENC_PICTYPE_B ? AV_PICTURE_TYPE_B : AV_PICTURE_TYPE_I;
        avctx->coded_frame->key_frame =
            nvenc_bitstream.pic_type == NVENC_PICTYPE_IDR ? 1 : 0;

        *got_packet = 1;
    }

    return 0;
}

static av_cold int ff_libnvenc_close(AVCodecContext *avctx)
{
    NvEncContext *nvenc_ctx = (NvEncContext*)avctx->priv_data;
    int i;

    av_freep(&avctx->extradata);

    for (i = 0; i < nvenc_ctx->nvenc_cfg.x264_paramc; i++)
        av_freep(&nvenc_ctx->nvenc_cfg.x264_paramv[i]);
    av_freep(&nvenc_ctx->nvenc_cfg.x264_paramv);

    // Destroy nvencoder
    if (nvenc_ctx->nvenc)
        nvenc_close(nvenc_ctx->nvenc);

    av_frame_free(&avctx->coded_frame);

    return 0;
}


// Options
#define OFFSET(x)   offsetof(NvEncContext, x)
#define OPTIONS     AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "preset"         , "Set x264 encoding preset"       , OFFSET(preset)          , AV_OPT_TYPE_STRING, { .str = "medium" }   , 0, 0, OPTIONS},
    { "tune"           , "Set x264 encoding tuning"       , OFFSET(tune)            , AV_OPT_TYPE_STRING, { 0 }                 , 0, 0, OPTIONS},
    { "profile"        , "Set H.264 profile restrictions.", OFFSET(profile)         , AV_OPT_TYPE_STRING, { 0 }                 , 0, 0, OPTIONS},
    { "fastfirstpass"  , "Ignored."                       , OFFSET(fastfirstpass)   , AV_OPT_TYPE_INT   , { .i64 = 1 }          , 0, 1, OPTIONS},
    { "level"          , "Set H.264 level"                , OFFSET(level)           , AV_OPT_TYPE_STRING, { .str=NULL}          , 0, 0, OPTIONS},
    { "passlogfile"    , "Ignored."                       , OFFSET(stats)           , AV_OPT_TYPE_STRING, { .str=NULL}          , 0, 0, OPTIONS},
    { "wpredp"         , "Ignored."                       , OFFSET(wpredp)          , AV_OPT_TYPE_STRING, { .str=NULL}          , 0, 0, OPTIONS},
    { "crf"            , "Ignored."                       , OFFSET(crf)             , AV_OPT_TYPE_FLOAT , { .dbl = -1 }         , -1, FLT_MAX, OPTIONS },
    { "crf_max"        , "Ignored."                       , OFFSET(crf_max)         , AV_OPT_TYPE_FLOAT , { .dbl = -1 }         , -1, FLT_MAX, OPTIONS },
    { "qp"             , "Constant quantization parameter", OFFSET(cqp)             , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, INT_MAX, OPTIONS },
    { "aq-mode"        , "Ignored."                       , OFFSET(aq_mode)         , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, INT_MAX, OPTIONS, "aq_mode"},
    { "aq-strength"    , "Ignored."                       , OFFSET(aq_strength)     , AV_OPT_TYPE_FLOAT , {.dbl = -1}           , -1, FLT_MAX, OPTIONS},
    { "psy"            , "Ignored."                       , OFFSET(psy)             , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS },
    { "psy-rd"         , "Ignored."                       , OFFSET(psy_rd)          , AV_OPT_TYPE_STRING, { 0 }                 , 0, 0, OPTIONS},
    { "rc-lookahead"   , "Ignored."                       , OFFSET(rc_lookahead)    , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, INT_MAX, OPTIONS },
    { "weightb"        , "Ignored."                       , OFFSET(weightb)         , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS },
    { "weightp"        , "Ignored."                       , OFFSET(weightp)         , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, INT_MAX, OPTIONS, "weightp" },
    { "ssim"           , "Ignored."                       , OFFSET(ssim)            , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS },
    { "intra-refresh"  , "Use Periodic Intra Refresh."    , OFFSET(intra_refresh)   , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS },
    { "bluray-compat"  , "Ignored."                       , OFFSET(bluray_compat)   , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS },
    { "b-bias"         , "Ignored."                       , OFFSET(b_bias)          , AV_OPT_TYPE_INT   , { .i64 = INT_MIN}     , INT_MIN, INT_MAX, OPTIONS },
    { "b-pyramid"      , "Ignored."                       , OFFSET(b_pyramid)       , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, INT_MAX, OPTIONS, "b_pyramid" },
    { "mixed-refs"     , "Ignored."                       , OFFSET(mixed_refs)      , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS },
    { "8x8dct"         , "High profile 8x8 transform."    , OFFSET(dct8x8)          , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS},
    { "fast-pskip"     , "Ignored."                       , OFFSET(fast_pskip)      , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS},
    { "aud"            , "Insert access unit delimiters." , OFFSET(aud)             , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS},
    { "mbtree"         , "Ignored."                       , OFFSET(mbtree)          , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, 1, OPTIONS},
    { "deblock"        , "Loop filter, in <alpha:beta>."  , OFFSET(deblock)         , AV_OPT_TYPE_STRING, { 0 },  0, 0          , OPTIONS},
    { "cplxblur"       , "Ignored."                       , OFFSET(cplxblur)        , AV_OPT_TYPE_FLOAT , { .dbl = -1 }         , -1, FLT_MAX, OPTIONS},
    { "partitions"     , "Ignored."                       , OFFSET(partitions)      , AV_OPT_TYPE_STRING, { 0 }, 0, 0           , OPTIONS},
    { "direct-pred"    , "Direct MV prediction mode"      , OFFSET(direct_pred)     , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, INT_MAX, OPTIONS, "direct-pred" },
    { "slice-max-size" , "Ignored."                       , OFFSET(slice_max_size)  , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, INT_MAX, OPTIONS },
    { "stats"          , "Ignored."                       , OFFSET(stats)           , AV_OPT_TYPE_STRING, { 0 }                 ,  0,       0, OPTIONS },
    { "nal-hrd"        , "Insert HRD info NALUs"          , OFFSET(nal_hrd)         , AV_OPT_TYPE_INT   , { .i64 = -1 }         , -1, INT_MAX, OPTIONS, "nal-hrd" },
    { "x264opts"       , "Apply x264-style options using a :-separated list of key=value pairs", OFFSET(x264_opts)              , AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, OPTIONS},
    { "x264-params"    , "Apply x264-style options using a :-separated list of key=value pairs", OFFSET(x264_params)            , AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, OPTIONS },
    { NULL }           ,
};
static const AVCodecDefault nvenc_defaults[] = {
    { "qmin" , "-1"    },
    { "qmax" , "-1"    },
    { "refs" , "-1"    },
    { "flags", "+cgop" },
    { NULL },
};
static const AVClass nvenc_class = {
    .class_name = "NVIDIA NVENC",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libnvenc_encoder = {
    .name             = "libnvenc",
    .long_name        = NULL_IF_CONFIG_SMALL("libnvenc H.264 / MPEG-4 AVC"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_H264,
    .priv_data_size   = sizeof(NvEncContext),
    .capabilities     = 0,
    .pix_fmts         = nvenc_pix_fmts,
    .priv_class       = &nvenc_class,
    .defaults         = nvenc_defaults,
    .init             = ff_libnvenc_init,
    .encode2          = ff_libnvenc_encode,
    .close            = ff_libnvenc_close,
};
