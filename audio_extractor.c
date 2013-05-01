#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

int main(int argc, char *argv[]) {
    static AVFormatContext *input_format_context = NULL, *output_format_context = NULL;
    static AVStream *input_audio_stream, *output_audio_stream;
    static AVPacket packet, new_packet;
    int i, audio_stream_id;
    char *input_filename, *output_filename;

    if (argc < 3) {
        printf("Incorrect arguments\n");
        exit(1);
    }

    input_filename = argv[1];
    output_filename = argv[2];

    // setup
    av_register_all();
    packet.data = NULL;
    packet.size = 0;
    avformat_open_input(&input_format_context, input_filename, NULL, NULL);

    if (avformat_find_stream_info(input_format_context, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    audio_stream_id = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    input_audio_stream = input_format_context->streams[audio_stream_id];

    output_format_context = avformat_alloc_context();
    output_format_context->oformat = av_guess_format("adts", NULL, NULL);
    snprintf(output_format_context->filename, sizeof(output_format_context->filename), "%s", output_filename);

    output_audio_stream = avformat_new_stream(output_format_context, NULL);

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

            av_free_packet(&packet);
        }
    }
    av_write_trailer(output_format_context);
    avio_close(output_format_context->pb);
}
