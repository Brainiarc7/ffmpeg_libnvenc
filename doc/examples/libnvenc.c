#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

#if _WIN32
#define __STDC_CONSTANT_MACROS
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>

#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(int argc, char* argv[])
{
    char* inputfile   = NULL;
    char* preset      = "default";
    bool  zerolatency = false;

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-input") == 0) {
            inputfile = argv[++i];
        }
        else if(strcmp(argv[i], "-preset") == 0) {  
            preset = argv[++i];
        }
        else if(strcmp(argv[i], "-zerolatency") == 0) {
            zerolatency = true;
        }
    }

    if(inputfile == NULL) {
        printf("Usage: libnvenc_sample -input your.input -preset [default]/slow/fast\n");
        return 0;
    }

    //ffmpeg decoder
    av_register_all();
    //init decode context  
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext  *dec_ctx = NULL;

    if(avformat_open_input(&fmt_ctx, inputfile, NULL, NULL) < 0)
    {
        printf("can't open the file %s\n", inputfile);
        return -1;
    }

    if(avformat_find_stream_info(fmt_ctx, NULL)<0)
    {
        printf("can't find suitable codec parameters\n");
        return -1;
    }

    AVStream *vst_dec;
    AVCodec *dec = NULL;
    int ret;
    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0) {
        printf("Could not find video stream in input file!\n");
        return -1;
    } 
    else {
        vst_dec = fmt_ctx->streams[video_stream_idx];

        dec_ctx = vst_dec->codec;
        dec = avcodec_find_decoder(dec_ctx->codec_id);

        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
            return -1;
        }

        if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
            return -1;
        }
    }

    // encode context          
    AVFormatContext* oc;
    const char* filename = "out.mp4";
    avformat_alloc_output_context2(&oc, NULL, NULL, filename); 
    if (!oc) {
        printf("FFMPEG: avformat_alloc_context error\n");
        return -1;
    }

    if ((ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE)) < 0) {
        fprintf(stderr, "Could not open '%s'\n", filename);
        return -1;
    }  

    AVCodec* enc = avcodec_find_encoder_by_name("libnvenc");
    if(!enc) {
        printf("Could not find libnvenc\n");
        printf("Please run ./configure --enable-libnvenc and make ffmpeg again\n");
        return -1;
    }
    AVStream* vst_enc = avformat_new_stream(oc, enc);
    if (!vst_enc) {
        printf("FFMPEG: Could not alloc video stream");
        return -1;
    }

    AVCodecContext* enc_ctx = vst_enc->codec;

    double bitrate = (double)5000000 * (dec_ctx->width * dec_ctx->height) / (1280*720);       
    enc_ctx->codec_id	   = enc->id;
    enc_ctx->codec_type	   = AVMEDIA_TYPE_VIDEO;
    enc_ctx->width		   = dec_ctx->width;
    enc_ctx->height        = dec_ctx->height;
    enc_ctx->pix_fmt       = dec_ctx->pix_fmt;
    enc_ctx->time_base.num = dec_ctx->time_base.num;
    enc_ctx->time_base.den = dec_ctx->time_base.den/2;           
    enc_ctx->bit_rate	   = bitrate;                
    enc_ctx->flags        |= (oc->oformat->flags & AVFMT_GLOBALHEADER) ? CODEC_FLAG_GLOBAL_HEADER : 0;
    av_opt_set(enc_ctx->priv_data, "preset", preset, 0);  // "fast" = HP, "slow" = HQ, default = LOW_LATENCY_DEFAULT
    if(zerolatency) {
        //use LOW_LATENCY preset
        av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);
    }

    ret = avcodec_open2(enc_ctx, enc, NULL);
    if (ret < 0) {
        printf("could not open codec\n");
        return -1;
    }
    ret = avformat_write_header(oc, NULL);

    AVPacket dec_pkt;
    av_init_packet(&dec_pkt);
    dec_pkt.data = NULL;
    dec_pkt.size = 0;

    AVFrame *frame = avcodec_alloc_frame();
    int got_frame = 0;
    while(av_read_frame(fmt_ctx, &dec_pkt) >= 0) {  
        if(dec_pkt.stream_index == video_stream_idx) {
            if(avcodec_decode_video2(dec_ctx, frame, &got_frame, &dec_pkt) < 0) {
                printf("Error decoding frames!\n");
                return -1;
            }
            if(got_frame) {
                AVPacket enc_pkt;
                int got_pkt = 0;
                av_init_packet(&enc_pkt);
                enc_pkt.data = NULL;
                enc_pkt.size = 0;       
                if(avcodec_encode_video2(vst_enc->codec, &enc_pkt, frame, &got_pkt) < 0) {
                    printf("Error encoding frames!\n");
                    return -1;
                }

                if(got_pkt) {
                    av_write_frame(oc, &enc_pkt);
                }

                av_free_packet(&enc_pkt);  
            }
        }
        av_free_packet(&dec_pkt);             
    }
    avcodec_free_frame(&frame);
    av_write_trailer(oc);

    avcodec_close(dec_ctx);
    avcodec_close(enc_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
}