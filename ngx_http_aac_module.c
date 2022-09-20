#include "ngx_http_aac_module.h"

static ngx_int_t ngx_http_aac_handler(ngx_http_request_t *r) {
    int             status_code;
    ngx_int_t       rc;
    ngx_buf_t       *b;
    ngx_chain_t     out;
    ngx_str_t       rootpath = ngx_null_string;
    ngx_str_t       outputformat = ngx_null_string;
    ngx_str_t       outputheader = ngx_null_string;
    ngx_str_t       source = ngx_null_string;
    audio_buffer    *destination;
    ngx_http_aac_module_loc_conf_t *conf;

    destination = ngx_pcalloc(r->pool, sizeof(audio_buffer));
    destination->data = NULL;
    destination->len = 0;
    destination->pool = r->pool;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_aac_module);
    ngx_http_complex_value(r, conf->videosegments_rootpath, &rootpath);
    ngx_http_complex_value(r, conf->output_format, &outputformat);
    ngx_http_complex_value(r, conf->output_header, &outputheader);

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    source = build_source_path(r->pool, rootpath, r->uri);
    status_code = ngx_http_aac_extract_audio(r->pool, r->connection->log, source, outputformat, destination);

    if (status_code == NGX_HTTP_AAC_MODULE_VIDEO_SEGMENT_NOT_FOUND) {
        r->headers_out.status = NGX_HTTP_NOT_FOUND;
        r->header_only = 1;
        r->headers_out.content_length_n = 0;

    } else if (status_code == NGX_HTTP_AAC_MODULE_AUDIO_STREAM_NOT_FOUND) {
        r->headers_out.status = NGX_HTTP_NO_CONTENT;
        r->header_only = 1;
        r->headers_out.content_length_n = 0;

    } else if (status_code == NGX_HTTP_AAC_MODULE_NO_DECODER) {
        r->headers_out.status = NGX_HTTP_NOT_IMPLEMENTED;
        r->header_only = 1;
        r->headers_out.content_length_n = 0;

    } else {
        r->headers_out.content_type_len = outputheader.len;
        r->headers_out.content_type.len = r->headers_out.content_type_len;
        r->headers_out.content_type.data = outputheader.data;

        b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        out.buf = b;
        out.next = NULL;

        b->pos = destination->data;
        b->last = destination->data + (destination->len * sizeof(unsigned char));
        b->memory = 1;
        b->last_buf = 1;

        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n = destination->len;
    }

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}

static void *ngx_http_aac_module_create_loc_conf(ngx_conf_t *cf) {
    ngx_http_aac_module_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_aac_module_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->videosegments_rootpath = NULL;
    conf->output_format = NULL;
    conf->output_header = NULL;
    conf->enabled = NGX_CONF_UNSET;

    return conf;
}

static char *ngx_http_aac_module_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
    ngx_http_aac_module_loc_conf_t *prev = parent;
    ngx_http_aac_module_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);

    if (conf->videosegments_rootpath == NULL) {
        conf->videosegments_rootpath = (ngx_http_complex_value_t *)prev->videosegments_rootpath;
    }

    if (conf->output_format == NULL) {
        conf->output_format = (ngx_http_complex_value_t *)prev->output_format;
    }

    if (conf->output_header == NULL) {
        conf->output_header = (ngx_http_complex_value_t *)prev->output_header;
    }

    if ((conf->videosegments_rootpath == NULL) && (conf->enabled == 1)) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "aac module: videosegments rootpath must be defined");
        return NGX_CONF_ERROR;
    }

    if ((conf->output_format == NULL) && (conf->enabled == 1)) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "aac module: output format must be defined");
        return NGX_CONF_ERROR;
    }

    if ((conf->output_header == NULL) && (conf->enabled == 1)) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "aac module: output header must be defined");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *ngx_http_aac(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_core_loc_conf_t *clcf;
    ngx_http_aac_module_loc_conf_t *vtlcf = conf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_aac_handler;

    vtlcf->enabled = 1;

    return NGX_CONF_OK;
}

static int ngx_http_aac_extract_audio(ngx_pool_t *pool, ngx_log_t  *log, ngx_str_t source, ngx_str_t outputfmt, audio_buffer *destination) {
    int    audio_stream_id;
    int    return_code = NGX_ERROR;
    int    buffer_size;
    unsigned char *exchange_area;

    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;
    AVOutputFormat *ofmt = NULL;
    AVStream *input_audio_stream;
    AVStream *output_audio_stream;
    AVCodec *in_codec;
    AVCodecContext *out_codec_ctx;
    AVPacket packet;
    AVIOContext *io_context;
    int ret;

    av_register_all();
    av_log_set_level(AV_LOG_PANIC);

    if (avformat_open_input(&input_format_context, (char *)source.data, NULL, NULL) < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "aac module: could not open video input: '%s'", source.data);
        return_code = NGX_HTTP_AAC_MODULE_VIDEO_SEGMENT_NOT_FOUND;
        goto exit;
    }

    if (avformat_find_stream_info(input_format_context, NULL) < 0) {
        goto exit;
        ngx_log_error(NGX_LOG_ERR, log, 0, "aac module: could not find stream info");
    }

    audio_stream_id = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_id == AVERROR_STREAM_NOT_FOUND) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "aac module: audio stream not found");
        return_code = NGX_HTTP_AAC_MODULE_AUDIO_STREAM_NOT_FOUND;
        goto exit;
    } else if (audio_stream_id == AVERROR_DECODER_NOT_FOUND) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "aac module: audio stream found, but no decoder for it");
        return_code = NGX_HTTP_AAC_MODULE_NO_DECODER;
        goto exit;
    }

    input_audio_stream = input_format_context->streams[audio_stream_id];
    output_format_context = avformat_alloc_context();
    if (!output_format_context) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "aac module: could not alloc output context");
        goto exit;
    }
    in_codec = avcodec_find_decoder(input_audio_stream->codecpar->codec_id);
    output_audio_stream = avformat_new_stream(output_format_context, in_codec);
    if (!output_audio_stream) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "aac module: could not alloc output audio stream");
        goto exit;
    }

    buffer_size = 4096;//1024;
    exchange_area = ngx_pcalloc(pool, buffer_size * sizeof(unsigned char));
    io_context = avio_alloc_context(exchange_area, buffer_size, 1, (void *)destination, NULL, write_packet, NULL);
    output_format_context->pb = io_context;

    ofmt = av_guess_format((char *)outputfmt.data, NULL, NULL);
    if (!ofmt) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "aac module: could not get output format '%V'", &outputfmt);
        goto exit;
    }
    output_format_context->oformat = ofmt;

    out_codec_ctx = avcodec_alloc_context3(in_codec);
    ret = avcodec_parameters_to_context(out_codec_ctx, input_audio_stream->codecpar);
    if (ret < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "Failed to copy context input to output stream codec context");
        goto exit;
    }

    out_codec_ctx->codec_tag = 0;
    if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        out_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_parameters_from_context(output_audio_stream->codecpar, out_codec_ctx);
    if (ret < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "Failed to copy context input to output stream codec context: %d(%s)", ret, av_err2str(ret));
        goto exit;
    }

    ret = avformat_write_header(output_format_context, NULL);
    if (ret < 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "Failed to avformat_write_header: %d(%s)", ret, av_err2str(ret));
        goto exit;
    }

    //av_dump_format(output_format_context, 0, "in_memory.aac", 1);

    av_init_packet(&packet);
    packet.size = 0;
    packet.data = NULL;
    while (av_read_frame(input_format_context, &packet) >= 0) {
        if (packet.stream_index == audio_stream_id) {

            /* copy packet */
            packet.pts = av_rescale_q_rnd(packet.pts, input_audio_stream->time_base, output_audio_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            packet.dts = av_rescale_q_rnd(packet.dts, input_audio_stream->time_base, output_audio_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            packet.duration = av_rescale_q(packet.duration, input_audio_stream->time_base, output_audio_stream->time_base);
            packet.pos = -1;

            packet.stream_index = 0; // force set to 1st stream

            ret = av_interleaved_write_frame(output_format_context, &packet);
            if (ret < 0) {
                ngx_log_error(NGX_LOG_ERR, log, 0, "Error muxing packet, %d(%s)", ret, av_err2str(ret));
                goto exit;
            }
        }
        av_packet_unref(&packet);
    }
    av_write_trailer(output_format_context);
    av_free(io_context);

    return_code = NGX_OK;
exit:
    /* do some cleanup */
    if (output_format_context != NULL) avformat_free_context(output_format_context);
    if (input_format_context != NULL) avformat_close_input(&input_format_context);

    return return_code;
}

/*
 * Remove extension from uri and paste rootpath, uri and ts extension for get source path
 */
ngx_str_t build_source_path(ngx_pool_t *pool, ngx_str_t rootpath, ngx_str_t uri) {
    int len = 0;
    int urilen = uri.len;
    char *source;
    ngx_str_t source_t;

    u_char *dot = (u_char *) ngx_strchr(uri.data, '.');
    u_char *slash = (u_char *) ngx_strchr(uri.data, '/');

    if (slash!=NULL && dot!=NULL && dot>slash) {
        // We need to remove extension
        urilen = uri.len - (uri.data + uri.len - dot);
    }

    len = (rootpath.len + urilen + strlen(".ts"));
    source = ngx_pcalloc(pool, len * sizeof(char));
    strncpy(source, (char *)rootpath.data, rootpath.len);
    strncat(source, (char *)uri.data, urilen );
    strcat(source, ".ts");

    source_t.data = (u_char *)source;
    source_t.len = len;
    return source_t;
}

static int write_packet(void *opaque, unsigned char *buf, int buf_size) {
    int old_size;
    audio_buffer *destination = (audio_buffer *)opaque;

    if (destination->data == NULL) {
        destination->data = ngx_pcalloc(destination->pool, NGX_AAC_AUDIO_CHUNK_MAX_SIZE * sizeof(unsigned char));
    }

    old_size = destination->len;
    destination->len += buf_size;
    memcpy(destination->data + old_size, buf, buf_size * sizeof(unsigned char));

    return buf_size;
}
