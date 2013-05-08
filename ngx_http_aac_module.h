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


typedef struct {
  ngx_http_complex_value_t *videosegments_rootpath;
  ngx_flag_t enabled;
} ngx_http_aac_module_loc_conf_t;


static char *ngx_http_aac(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static int write_packet(void *opaque, unsigned char *buf, int buf_size);

static int ngx_http_aac_extract_audio(ngx_log_t  *log, ngx_str_t source, audio_buffer *destination);

ngx_str_t build_source_path(ngx_str_t rootpath, ngx_str_t uri);

static void* ngx_http_aac_module_create_loc_conf(ngx_conf_t *cf);

static char* ngx_http_aac_module_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_command_t ngx_http_aac_commands[] = {
    { ngx_string("ngx_hls_audio_track"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_aac,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ngx_hls_audio_track_rootpath"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_aac_module_loc_conf_t, videosegments_rootpath),
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

    ngx_http_aac_module_create_loc_conf, /* create location configuration */
    ngx_http_aac_module_merge_loc_conf /* merge location configuration */
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

