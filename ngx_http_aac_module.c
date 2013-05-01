#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

static char *ngx_http_aac(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static int ngx_http_aac_extract_audio(ngx_http_request_t *r, const char *input_filename, const char *output_filename);

static ngx_command_t ngx_http_aac_commands[] = {
    { ngx_string("build_audio_track"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_aac,
      0,
      0,
      NULL },
    ngx_null_command
};


static ngx_http_module_t ngx_http_aac_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */

    NULL,                          /* create location configuration */
    NULL                           /* merge location configuration */
};


ngx_module_t ngx_http_aac_module = {
    NGX_MODULE_V1,
    &ngx_http_aac_module_ctx,    /* module context */
    ngx_http_aac_commands,       /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};

static u_char ngx_hello_string[] = "Hello, world!";

static ngx_int_t ngx_http_aac_handler(ngx_http_request_t *r) {
    ngx_int_t    rc;
    ngx_buf_t   *b;
    ngx_chain_t  out;

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    ngx_http_aac_extract_audio(r, "/tmp/segment.ts", "output.aac");

    /* set the 'Content-type' header */
    r->headers_out.content_type_len = sizeof("text/html") - 1;
    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";


    /* allocate a buffer for your response body */
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* attach this buffer to the buffer chain */
    out.buf = b;
    out.next = NULL;

    /* adjust the pointers of the buffer */
    b->pos = ngx_hello_string;
    b->last = ngx_hello_string + sizeof(ngx_hello_string) - 1;
    b->memory = 1;    /* this buffer is in memory */
    b->last_buf = 1;  /* this is the last buffer in the buffer chain */

    /* set the status line */
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = sizeof(ngx_hello_string) - 1;

    /* send the headers of your response */
    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    /* send the buffer chain of your response */
    return ngx_http_output_filter(r, &out);
}


static char *ngx_http_aac(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_core_loc_conf_t *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_aac_handler;

    return NGX_CONF_OK;
}

static int ngx_http_aac_extract_audio(ngx_http_request_t *r, const char *input_filename, const char *output_filename) {
    int audio_stream_id;
    int return_code = NGX_ERROR;
    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;
    AVStream *input_audio_stream;
    AVStream *output_audio_stream;
    AVPacket packet, new_packet;

    av_register_all();
    packet.data = NULL;
    packet.size = 0;

    if (avformat_open_input(&input_format_context, input_filename, NULL, NULL) < 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "aac module: could not open video input: %s", input_filename);
        goto exit;
    }

    if (avformat_find_stream_info(input_format_context, NULL) < 0) {
        goto exit;
    }

    audio_stream_id = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_id == AVERROR_STREAM_NOT_FOUND) {
        // should return 404 to user? issue #2
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "aac module: audio stream not found");
        goto exit;
    } else if (audio_stream_id == AVERROR_DECODER_NOT_FOUND) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "aac module: audio stream found, but no decoder for it");
        goto exit;
    }

    input_audio_stream = input_format_context->streams[audio_stream_id];
    output_format_context = avformat_alloc_context();
    if (!output_format_context) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "aac module: could not alloc output context");
        goto exit;
    }

    output_format_context->oformat = av_guess_format("adts", NULL, NULL);
    snprintf(output_format_context->filename, sizeof(output_format_context->filename), "%s", output_filename);

    output_audio_stream = avformat_new_stream(output_format_context, NULL);
    if (!output_audio_stream) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "aac module: could not alloc output audio stream");
        goto exit;
    }

    avcodec_copy_context(output_audio_stream->codec, input_audio_stream->codec);
    avio_open(&output_format_context->pb, output_filename, AVIO_FLAG_WRITE);
    avformat_write_header(output_format_context, NULL);

    while (av_read_frame(input_format_context, &packet) >= 0) {
        if (packet.stream_index == audio_stream_id) {
            av_init_packet(&new_packet);
            new_packet.pts = packet.pts;
            new_packet.dts = packet.dts;
            /* avformat_new_stream creates audio stream at index 0,
               so the packets need to be written at this index */
            new_packet.stream_index = 0;
            new_packet.data = packet.data;
            new_packet.size = packet.size;

            av_interleaved_write_frame(output_format_context, &new_packet) ;
            av_free_packet(&new_packet);
        }
        av_free_packet(&packet);
    }

    av_write_trailer(output_format_context);
    avio_close(output_format_context->pb);
    return_code = NGX_OK;

exit:
    /* do some cleanup */
    if (output_format_context != NULL) avformat_free_context(output_format_context);
    if (input_format_context != NULL) avformat_free_context(input_format_context);

    return return_code;
}
