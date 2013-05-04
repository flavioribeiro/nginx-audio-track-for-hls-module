#include <string.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

typedef struct {
    unsigned char *data;
    int len;
} audio_buffer;

static char *ngx_http_aac(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static int write_packet(void *opaque, unsigned char *buf, int buf_size);
static int ngx_http_aac_extract_audio(ngx_http_request_t *r, audio_buffer *output_buffer);
char *change_file_extension(char *input_filename, int size);

static ngx_command_t ngx_http_aac_commands[] = {
    { ngx_string("return_audio_track"),
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

static ngx_int_t ngx_http_aac_handler(ngx_http_request_t *r) {
    ngx_int_t    rc;
    ngx_buf_t   *b;
    ngx_chain_t  out;
    audio_buffer  *output_buffer = malloc(sizeof(audio_buffer));

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    /* set the 'Content-type' header */
    r->headers_out.content_type_len = sizeof("audio/aac") - 1;
    r->headers_out.content_type.len = sizeof("audio/aac") - 1;
    r->headers_out.content_type.data = (u_char *) "audio/aac";


    /* allocate a buffer for your response body */
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* initialize audio output buffer */
    output_buffer->data = NULL;
    output_buffer->len = 0;

    ngx_http_aac_extract_audio(r, output_buffer);

    /* attach this buffer to the buffer chain */
    out.buf = b;
    out.next = NULL;

    /* adjust the pointers of the buffer */
    b->pos = output_buffer->data;
    b->last = output_buffer->data + (output_buffer->len * sizeof(unsigned char));
    b->memory = 1;    /* this buffer is in memory */
    b->last_buf = 1;  /* this is the last buffer in the buffer chain */

    /* set the status line */
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = output_buffer->len;

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

static int ngx_http_aac_extract_audio(ngx_http_request_t *r, audio_buffer *output_buffer) {
    int audio_stream_id;
    int return_code = NGX_ERROR;
    int buffer_size;
    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;
    AVStream *input_audio_stream;
    AVStream *output_audio_stream;
    AVPacket packet, new_packet;
    AVIOContext *io_context;
    char *input_filename;
    unsigned char *exchange_area;

    input_filename = change_file_extension((char *)r->uri.data, r->uri.len);

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

    output_audio_stream = avformat_new_stream(output_format_context, NULL);
    if (!output_audio_stream) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "aac module: could not alloc output audio stream");
        goto exit;
    }

    buffer_size = 1024;
    exchange_area = (unsigned char*)av_malloc(buffer_size*sizeof(unsigned char));

    io_context = avio_alloc_context(exchange_area, buffer_size, 1, (void *)output_buffer, NULL, write_packet, NULL);
    output_format_context->pb = io_context;
    output_format_context->oformat = av_guess_format("adts", NULL, NULL);

    avcodec_copy_context(output_audio_stream->codec, input_audio_stream->codec);
    avformat_write_header(output_format_context, NULL);

    while (av_read_frame(input_format_context, &packet) >= 0) {
        if (packet.stream_index == audio_stream_id) {
            av_init_packet(&new_packet);
            new_packet.stream_index = 0;
            new_packet.pts = packet.pts;
            new_packet.dts = packet.dts;
            new_packet.data = packet.data;
            new_packet.size = packet.size;

            av_interleaved_write_frame(output_format_context, &new_packet) ;
            av_free_packet(&new_packet);
        }
        av_free_packet(&packet);
    }

    av_write_trailer(output_format_context);
    av_free(exchange_area);
    av_free(io_context);

    return_code = NGX_OK;

exit:
    /* do some cleanup */
    if (output_buffer->data != NULL) av_free(output_buffer->data);
    if (input_filename != NULL) av_free(input_filename);
    if (output_format_context != NULL) avformat_free_context(output_format_context);
    if (input_format_context != NULL) avformat_close_input(&input_format_context);

    return return_code;
}

char *change_file_extension(char *input_filename, int size) {
    input_filename[size] = '\0';
    char *new_filename = av_mallocz((size - 1) * sizeof(char));
    strncpy(new_filename, input_filename, size - 3);
    strcat(new_filename, "ts");
    new_filename[size - 1] = '\0';

    return new_filename;
}

static int write_packet(void *opaque, unsigned char *buf, int buf_size) {
    int old_size;
    audio_buffer *output_buffer = (audio_buffer *)opaque;

    old_size = output_buffer->len;
    output_buffer->len += buf_size;

    /* increase output_buffer->data */
    output_buffer->data = av_realloc(output_buffer->data, output_buffer->len * sizeof(unsigned char)); //TODO improve realloc

    /* copy the new data in buf to output_buffer->data + old+size which is the begin of concatenation */
    memcpy(output_buffer->data + old_size, buf, buf_size * sizeof(unsigned char));

    return buf_size;
}
