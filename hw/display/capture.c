#include "capture.h"

static void sound_capture_notify(void *opaque, audcnotification_e cmd)
{
    (void) opaque;
    (void) cmd;
}

static void sound_capture_destroy(void *opaque)
{
    (void) opaque;
}

static void write_empty_sound(void *opaque, struct CaptureThreadWorkerData *data)
{
    AVFormatContext *oc = data->oc;
    OutputStream *ost = &data->audio_stream;

    AVFrame *tmp = ost->tmp_frame;
    ost->tmp_frame = ost->empty_frame;
    double newlen = write_audio_frame(oc, ost);
    ost->tmp_frame = tmp;

    if (newlen >= 0.0) {
        data->video_len = newlen;
    }
}

static void sound_capture_capture(void *opaque, void *buf, int size)
{
    int bufsize = size;
    SoundCapture *wav = opaque;
    AVFrame *frame;
    int sampleCount;
    double len1, len2, delta;
    int8_t *q;
    int buffpos;

    /*int32_t n = 0;
    int i = 0;
    for(i=0;i<size;i++) {
        int8_t a = ((int8_t*)buf)[i];
        n+=a;
    }
    wav->bytes += size;
    if(n==0)
        return;
    printf("%d\n",n);*/
    frame = wav->data->audio_stream.tmp_frame;
    sampleCount = frame->nb_samples * 4;

    len1 = wav->data->video_len;
    len2 = wav->data->video_len2;
    delta = len1 - len2;

    while (delta < 0.0) {
        write_empty_sound(opaque, wav->data);

        len1 = wav->data->video_len;
        len2 = wav->data->video_len2;
        delta = len1 - len2;
    }

    q = (int8_t *)frame->data[0];

    buffpos = 0;
    while (bufsize > 0) {
        int start = wav->bufferPos;
        int freeSpace = sampleCount - start;

        int willWrite = freeSpace;
        if (willWrite > bufsize) {
            willWrite = bufsize;
        }

        memcpy(q + start, buf + buffpos, willWrite);
        bufsize -= willWrite;
        buffpos += willWrite;

        freeSpace = sampleCount - start - willWrite;

        if (freeSpace == 0) {
            double newlen = write_audio_frame(wav->data->oc, &wav->data->audio_stream);

            if (newlen >= 0.0) {
                wav->data->video_len = newlen;
            }
            wav->bufferPos = 0;
        } else {
            wav->bufferPos = start + willWrite;
        }
    }
}

static void sound_capture_capture_destroy(void *opaque)
{
    SoundCapture *wav = opaque;

    AUD_del_capture (wav->cap, wav);
}

static int sound_capture_start_capture(struct CaptureThreadWorkerData *data)
{
    Monitor *mon = cur_mon;
    SoundCapture *wav;
    struct audsettings as;
    struct audio_capture_ops ops;
    CaptureVoiceOut *cap;

    as.freq = 44100;
    as.nchannels = 2;
    as.fmt = AUD_FMT_S16;
    as.endianness = 0;

    ops.notify = sound_capture_notify;
    ops.capture = sound_capture_capture;
    ops.destroy = sound_capture_destroy;

    wav = g_malloc0(sizeof(*wav));


    cap = AUD_add_capture(&as, &ops, wav);
    if (!cap) {
        monitor_printf(mon, "Failed to add audio capture\n");
        goto error_free;
    }

    wav->bufferPos = 0;
    wav->data = data;
    wav->cap = cap;
    data->soundCapture = wav;
    return 0;

error_free:
    g_free(wav);
    return -1;
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base,
                        AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;
    /* Write the compressed frame to the media file. */
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
static void add_video_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id,
                       int w, int h, int bit_rate, int framerate)
{
    AVCodecContext *c;
    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }
    ost->st = avformat_new_stream(oc, *codec);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams - 1;
    c = ost->st->codec;
    if ((*codec)->type == AVMEDIA_TYPE_VIDEO) {
        c->codec_id = codec_id;
        c->bit_rate = bit_rate;
        /* Resolution must be a multiple of two. */
        c->width    = w;
        c->height   = h;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){ 1, framerate };
        c->time_base = ost->st->time_base;
        c->gop_size  = 12; /* emit one intra frame every 12 frames at most */
        c->pix_fmt   = AV_PIX_FMT_YUV420P;
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
    } else {
        fprintf(stderr, "Wrong stream type\n");
        exit(1);
    }
    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
}

static void add_audio_stream(OutputStream *ost, AVFormatContext *oc,
                             AVCodec **codec,
                             enum AVCodecID codec_id)
{
    AVCodecContext *c;
    int i;
    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }
    ost->st = avformat_new_stream(oc, *codec);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams - 1;
    c = ost->st->codec;
    if ((*codec)->type == AVMEDIA_TYPE_AUDIO) {
        c->sample_fmt = AV_SAMPLE_FMT_FLTP;
        c->bit_rate    = 128000;
        c->sample_rate = 44100;
        c->channels    = av_get_channel_layout_nb_channels(c->channel_layout);
        c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO) {
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
                }
            }
        }
        c->channels   = av_get_channel_layout_nb_channels(c->channel_layout);
        ost->st->time_base = (AVRational){ 1, c->sample_rate };
    } else {
        fprintf(stderr, "Wrong stream type\n");
        exit(1);
    }
    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
}
/**************************************************************/
/* audio output */
static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  uint64_t channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;
    if (!frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }
    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;
    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }
    return frame;
}

static void open_audio(AVFormatContext *oc, AVCodec *codec,
                       OutputStream *ost, AVDictionary *opt_arg)
{
    AVCodecContext *c;
    int nb_samples;
    int ret;
    AVDictionary *opt = NULL;
    c = ost->st->codec;
    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }
    nb_samples = c->frame_size;
    ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
                                       c->sample_rate, nb_samples);

    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame->pts = 0;

    ost->empty_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout,
                                       c->sample_rate, nb_samples);
    int sampleCount = nb_samples * 4;
    int8_t *q = (int8_t *)ost->empty_frame->data[0];
    memset(q, 0, sampleCount);

    /* create resampler context */
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        exit(1);
    }
    /* set options */
    av_opt_set_int       (ost->swr_ctx, "in_channel_count",  c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",    c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",     AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int       (ost->swr_ctx, "out_channel_count", c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",   c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",    c->sample_fmt,     0);
    /* initialize the resampling context */
    if (swr_init(ost->swr_ctx) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        exit(1);
    }
}

/*
 * encode one audio frame and send it to the muxer
 */
static double write_audio_frame(AVFormatContext *oc, OutputStream *ost)
{
    AVCodecContext *c;
    AVPacket pkt = { 0 };
    AVFrame *frame;
    int ret;
    int got_packet;
    int dst_nb_samples;
    av_init_packet(&pkt);
    c = ost->st->codec;
    frame = ost->tmp_frame;
    frame->pts = frame->pts + 1;
    double videolen = -1.0;
    if (frame) {
        /* convert samples from native format to destination codec format,
         * using the resampler */
        /* compute destination number of samples */
        dst_nb_samples = av_rescale_rnd(
            swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples,
            c->sample_rate, c->sample_rate, AV_ROUND_UP);
        av_assert0(dst_nb_samples == frame->nb_samples);
        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0) {
            exit(1);
        }
        /* convert to destination format */
        ret = swr_convert(ost->swr_ctx,
                          ost->frame->data, dst_nb_samples,
                          (const uint8_t **)frame->data, frame->nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            exit(1);
        }
        frame = ost->frame;
        frame->pts = av_rescale_q(ost->samples_count,
                                  (AVRational){1, c->sample_rate},
                                  c->time_base);

        videolen = (double)frame->pts / c->sample_rate;
        ost->samples_count += dst_nb_samples;
    }
    ret = avcodec_encode_audio2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
        exit(1);
    }
    if (got_packet) {
        ret = write_frame(oc, &c->time_base, ost->st, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error while writing audio frame: %s\n",
                    av_err2str(ret));
            exit(1);
        }
    }
    return videolen;
}
static void write_delayed_audio_frames(void)
{
    struct CaptureThreadWorkerData *data = capture_get_data();
    AVFormatContext *oc = data->oc;
    OutputStream *ost = &data->audio_stream;
    AVCodecContext *c = ost->st->codec;

    AVPacket pkt = { 0 };
    pkt.data = NULL;
    pkt.size = 0;
    av_init_packet(&pkt);
    int got_output = 1;
    int ret;
    while (got_output) {

        ret = avcodec_encode_audio2(c, &pkt, NULL, &got_output);
        if (ret < 0) {
            fprintf(stderr, "Error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            ret = write_frame(oc, &c->time_base, ost->st, &pkt);
            av_free_packet(&pkt);
        }
    }
}
/**************************************************************/
/* video output */
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;
    picture = av_frame_alloc();
    if (!picture) {
        return NULL;
    }
    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;
    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }
    return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec,
                       OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->st->codec;
    AVDictionary *opt = NULL;
    av_dict_copy(&opt, opt_arg, 0);
    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }
    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
}

static AVFrame *get_filled_image(void)
{
    QemuConsole *con = qemu_console_lookup_by_index(0);
    DisplaySurface *surface;
    surface = qemu_console_surface(con);

    if (con == NULL) {
        fprintf(stderr, "There is no QemuConsole I can screendump from.\n");
        return NULL;
    }

    int ourW = pixman_image_get_width(surface->image);
    int ourH = pixman_image_get_height(surface->image);

    AVFrame *pict = alloc_picture(AV_PIX_FMT_RGB32, ourW, ourH);
    av_frame_make_writable(pict);

    uint8_t* picdata = (uint8_t *)pixman_image_get_data(surface->image);

    memcpy(pict->data[0], picdata, ourW * ourH * 4);
    return pict;
}

static AVFrame *get_video_frame(OutputStream *ost, int64_t frame)
{
    AVCodecContext *c = ost->st->codec;

    AVFrame *pict = get_filled_image();
    if (pict == NULL) {
        return NULL;
    }

    struct SwsContext *swsContext = sws_getContext(
        pict->width, pict->height, pict->format,
        ost->frame->width, ost->frame->height,
        ost->frame->format, SWS_BICUBIC, NULL, NULL, NULL);
    sws_scale(swsContext, (const uint8_t * const *)pict->data,
              pict->linesize , 0, c->height, ost->frame->data,
              ost->frame->linesize);

    av_frame_free(&pict);
    sws_freeContext(swsContext);

    if (frame <= ost->frame->pts) {
        ost->frame->pts = ost->frame->pts + 1;
    } else {
        ost->frame->pts = frame;
    }

    return ost->frame;
}
/*
 * encode one video frame and send it to the muxer
 */
static void write_video_frame(AVFormatContext *oc,
                              OutputStream *ost, int frameNumber)
{
    int ret;
    AVCodecContext *c;
    AVFrame *frame;
    int got_packet = 0;
    AVPacket pkt = { 0 };

    frame = get_video_frame(ost, frameNumber);
    if (frame == NULL) {
        return;
    }

    c = ost->st->codec;
    av_init_packet(&pkt);
    /* encode the image */
    ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
        exit(1);
    }
    if (got_packet) {
        ret = write_frame(oc, &c->time_base, ost->st, &pkt);
    } else {
        ret = 0;
    }
    if (ret < 0) {
        fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
        return;
    }
}

static void write_delayed_video_frames(void)
{
    struct CaptureThreadWorkerData *data = capture_get_data();
    AVFormatContext *oc = data->oc;
    OutputStream *ost = &data->stream;
    AVCodecContext *c = ost->st->codec;

    AVPacket pkt = { 0 };
    pkt.data = NULL;
    pkt.size = 0;
    av_init_packet(&pkt);
    int got_output = 1;
    int ret;
    while (got_output) {
        ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
        if (ret < 0) {
            fprintf(stderr, "Error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            ret = write_frame(oc, &c->time_base, ost->st, &pkt);
            av_free_packet(&pkt);
        }
    }
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_close(ost->st->codec);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

static int ends_with(const char *str, const char *suffix)
{
    if (!str || !suffix) {
        return 0;
    }
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr) {
        return 0;
    }
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

static struct CaptureThreadWorkerData *capture_get_data(void)
{
    static struct CaptureThreadWorkerData data = {0};
    return &data;
}

static void capture_timer(void *p)
{
    struct CaptureThreadWorkerData *data = (struct CaptureThreadWorkerData *)p;
    if (!data->is_capturing) {
        return;
    }

    int64_t n = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    int64_t intdelta = (n - data->time) / 100000;
    double delta = (double)intdelta / 10000;
    data->delta += delta;
    data->time   = n;

    while (data->delta > (1.0 / data->framerate)) {
        data->delta -= 1.0 / data->framerate;

        av_frame_make_writable(data->stream.frame);
        write_video_frame(data->oc, &data->stream,
            (int)(floor(data->video_len * (double)data->framerate + 0.5)));
    }
    data->video_len2 = data->video_len2 + delta;

    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    if (data->is_capturing) {
        timer_mod_ns(data->timer, now + 10000000);
    }
}

static void capture_powerdown_req(void)
{
    if (capture_stop()) {
        printf("Capture stoped\n");
    }
}

void hmp_capture_start(Monitor *mon, const QDict *qdict)
{
    const char *filename = qdict_get_str(qdict, "filename");
    int framerate = qdict_get_try_int(qdict, "fps", 60);

    struct CaptureThreadWorkerData *data = capture_get_data();
    if (!data->is_loaded) {
        av_register_all();
        avcodec_register_all();
        data->codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!data->codec) {
            fprintf(stderr, "codec not found\n");
            return;
        }
        data->c = NULL;
        data->is_loaded = 1;
        atexit(capture_powerdown_req);
    }

    if (data->is_capturing == 0) {
        if (!ends_with(filename, ".mp4")
         && !ends_with(filename, ".mpg")
         && !ends_with(filename, ".avi")) {
            monitor_printf(mon, "Invalid file format, use .mp4 or .mpg\n");
            return;
        }
        if (framerate != 60 && framerate != 30
         && framerate != 24 && framerate != 25) {
            monitor_printf(mon, "Invalid framerate, valid values are: 24, 25, 30, 60\n");
            return;
        }
        monitor_printf(mon, "Capture started to file: %s\n", filename);

        data->framerate = framerate;
        data->frame = 0;

        data->delta = 0.0;
        data->time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

        data->video_len = 0.0;
        data->video_len2 = 0.0;

        QemuConsole *con = qemu_console_lookup_by_index(0);
        DisplaySurface *surface;
        surface = qemu_console_surface(con);
        int resW = pixman_image_get_width(surface->image);
        int resH = pixman_image_get_height(surface->image);

        OutputStream video_st = { 0 };
        data->stream = video_st;
        OutputStream audio_st = { 0 };
        data->audio_stream = audio_st;

        avformat_alloc_output_context2(&data->oc, NULL, "avi", filename);
        AVOutputFormat *fmt;
        fmt = data->oc->oformat;

        add_video_stream(&data->stream, data->oc, &data->codec,
                         fmt->video_codec, resW, resH, 4000000, framerate);
        add_audio_stream(&data->audio_stream, data->oc, &data->audio_codec,
                         fmt->audio_codec);

        open_video(data->oc, data->codec, &data->stream, NULL);
        open_audio(data->oc, data->audio_codec, &data->audio_stream, NULL);

        int ret = avio_open(&data->oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return;
        }
        ret = avformat_write_header(data->oc, NULL);
        if (ret < 0) {
            fprintf(stderr, "Error occurred when opening output file: %s\n",
                    av_err2str(ret));
            return;
        }

        data->is_capturing = 1;

        if (data->timer) {
            timer_free(data->timer);
        }
        data->timer = timer_new_ns(QEMU_CLOCK_REALTIME, capture_timer, data);
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        timer_mod_ns(data->timer, now + 1000000000 / data->framerate);

        sound_capture_start_capture(data);
    } else {
        monitor_printf(mon, "Already capturing\n");
    }
}

static int capture_stop(void)
{
    struct CaptureThreadWorkerData *data = capture_get_data();
    if (!data->is_loaded) {
        return 0;
    }

    if (data->is_capturing) {
        data->is_capturing = 0;

        write_delayed_video_frames();
        write_delayed_audio_frames();

        av_write_trailer(data->oc);
        close_stream(data->oc, &data->stream);
        close_stream(data->oc, &data->audio_stream);
        avio_closep(&data->oc->pb);
        avformat_free_context(data->oc);

        sound_capture_capture_destroy(data->soundCapture);
        return 1;
    }
    return 0;
}

void hmp_capture_stop(Monitor *mon, const QDict *qdict)
{
    if (capture_stop()) {
        monitor_printf(mon, "Capture stopped\n");
    } else {
        monitor_printf(mon, "Not capturing\n");
    }
}
