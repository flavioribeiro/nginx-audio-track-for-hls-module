#!/bin/sh

#don't look at this line, it's just a workaround to me to static compile on my mac
gcc audio_extractor.c -o aextractor -I/opt/libav/include -L/opt/libav/lib -lavcodec_s -lavformat_s -lavutil_s -lz -lm -lbz2
