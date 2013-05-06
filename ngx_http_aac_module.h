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

