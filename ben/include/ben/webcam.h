#pragma once

#include <string>
#include "ffmpeg.h"
#include "viewer.h"

namespace ben {
  
  class Webcam : public ff::Util
  {
  private:
    std::string last_err_;

    AVInputFormat* input_format_ = nullptr;
    AVFormatContext* ifmt_ctx_ = nullptr;
    ff::StreamContext* stream_ctx_ = nullptr;
    AVFormatContext* ofmt_ctx_ = nullptr;
    ff::FilteringContext* filter_ctx_ = nullptr;

    Viewer viewer_;

  public:
    Webcam() {}

    ~Webcam()
    {
      close();
    }

    std::string& last_err()
    {
      return last_err_;
    }

    bool start_capture(
      const std::string& video_name,
      const std::string& audio_name,
      const std::string& output_filename
    ) {
      av_register_all();
      av_register_all();
      avfilter_register_all();
      avdevice_register_all();

      try {
        prepare_input(video_name, audio_name);
        prepare_output(output_filename);
        prepare_filter();
      } catch (std::runtime_error& e) {
        last_err_ = e.what();
        close();
        return false;
      }
      return true;
    }

    bool capturing()
    {
      try {
        capture_internal();
      }
      catch (std::runtime_error& e) {
        last_err_ = e.what();
        return false;
      }
      return true;
    }

    bool end_capture()
    {
      try {
        flush_filter_and_encoder();
        chk(av_write_trailer(ofmt_ctx_), "av_write_trailer");
      }
      catch (std::runtime_error& e) {
        last_err_ = e.what();
        return false;
      }
      return true;
    }

  private:
    void close()
    {
      for (unsigned int i = 0; i < ifmt_ctx_->nb_streams; i++) {
        avcodec_free_context(&stream_ctx_[i].dec_);
        if (ofmt_ctx_ && ofmt_ctx_->nb_streams > i && ofmt_ctx_->streams[i] && stream_ctx_[i].enc_) {
          avcodec_free_context(&stream_ctx_[i].enc_);
        }
        if (filter_ctx_ && filter_ctx_[i].filter_graph) {
          avfilter_graph_free(&filter_ctx_[i].filter_graph);
        }
      }
      av_free(filter_ctx_);
      av_free(stream_ctx_);
      avformat_close_input(&ifmt_ctx_);
      if (ofmt_ctx_ && !(ofmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmt_ctx_->pb);
      }
      avformat_free_context(ofmt_ctx_);
    }

    void flush_filter_and_encoder()
    {
      // flush filter and encoder
      for (unsigned int i = 0; i < ifmt_ctx_->nb_streams; i++) {
        //flush filter
        if (!filter_ctx_[i].filter_graph) {
          continue;
        }
        filter_encode_write_frame(ff::Frame(false), i);

        //flush encoder
        if (stream_ctx_[i].enc_->codec->capabilities & AV_CODEC_CAP_DELAY) {
          encode_write_frame(ff::Frame(false), i);
        }
      }
    }

    void capture_internal()
    {
      ff::Packet packet;

      chk(
        av_read_frame(ifmt_ctx_, packet),
        "capture av_read_frame"
      );

      int stream_index = packet->stream_index;

      AVMediaType type = ifmt_ctx_->streams[stream_index]->codecpar->codec_type;

      if (filter_ctx_[stream_index].filter_graph) {
        AVCodecContext* dec_ctx = stream_ctx_[stream_index].dec_;

        av_packet_rescale_ts(
          packet,
          ifmt_ctx_->streams[stream_index]->time_base,
          dec_ctx->time_base
        );

        // decode
        int ret = avcodec_send_packet(dec_ctx, packet);
        if (ret < 0) {
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
          }
          chk(ret, "avcodec_send_packet");
        }

        ff::Frame frame;
        chk(avcodec_receive_frame(dec_ctx, frame), "avcodec_receive_frame");

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
          viewer_.view(dec_ctx, frame);
        }

        frame->pts = av_frame_get_best_effort_timestamp(frame);
        filter_encode_write_frame(frame, stream_index);
      }
      else {
        // remux this frame without reencoding
        av_packet_rescale_ts(
          packet,
          ifmt_ctx_->streams[stream_index]->time_base,
          ofmt_ctx_->streams[stream_index]->time_base
        );

        chk(
          av_interleaved_write_frame(ofmt_ctx_, packet),
          "capture av_interleaved_write_frame"
        );
      }
    }

    void prepare_input(const std::string& video_name = "", const std::string audio_name = "")
    {
      AVDictionary* av_option = nullptr;
      av_dict_set(&av_option, "rtbufsize", "1000000000", NULL);
      input_format_ = av_find_input_format("dshow");
      ifmt_ctx_ = avformat_alloc_context();

      std::string device_name = "video=";
      device_name.append(video_name);
      if (!audio_name.empty()) {
        device_name.append(":audio=");
        device_name.append(audio_name);
      }

      // 파일일 경우 device_name 에 경로, input_format은 NULL;
      chk(
        avformat_open_input(&ifmt_ctx_, device_name.c_str(), input_format_, &av_option),
        "input avformat_open_input"
      );

      av_dict_free(&av_option);

      chk(
        avformat_find_stream_info(ifmt_ctx_, NULL),
        "input avformat_find_stream_info"
      );

      stream_ctx_ = (ff::StreamContext*)av_mallocz_array(ifmt_ctx_->nb_streams, sizeof(*stream_ctx_));
      chk(stream_ctx_, "input av_mallocz_array streams");


      for (unsigned int i = 0; i < ifmt_ctx_->nb_streams; i++) {

        AVStream* stream = ifmt_ctx_->streams[i];
        AVCodecParameters* dec_par = stream->codecpar;
        AVCodecID dec_id = dec_par->codec_id;

        AVCodec* dec = avcodec_find_decoder(dec_id);
        chk(
          dec, 
          "input avcodec_find_decoder[stream: %u, codec_id: %d]",
          i, static_cast<int>(dec_id)
        );

        AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
        chk(
          dec_ctx,
          "input avcodec_alloc_context3[stream: %u, codec_id: %d]",
          i, static_cast<int>(dec_id)
        );

        chk(
          avcodec_parameters_to_context(dec_ctx, dec_par),
          "input avcodec_parameters_to_context[stream: %u, codec_id: %d]",
          i, static_cast<int>(dec_id)
        );


        AVMediaType dec_type = dec_ctx->codec_type;
        if (
          dec_type == AVMEDIA_TYPE_VIDEO ||
          dec_type == AVMEDIA_TYPE_AUDIO
        ) {
          if (dec_type == AVMEDIA_TYPE_VIDEO) {
            dec_ctx->framerate = av_guess_frame_rate(ifmt_ctx_, stream, NULL);
          }

          chk(
            avcodec_open2(dec_ctx, dec, NULL),
            "input avcodec_open2[stream: %u, codec_id: %d]",
            i, static_cast<int>(dec_id)
          );

          if (dec_type == AVMEDIA_TYPE_VIDEO) {
            viewer_.init(dec_ctx);
          }
        }
        stream_ctx_[i].dec_ = dec_ctx;
      }

      //av_dump_format(ifmt_ctx_, 0, device_name.c_str(), 0);
    }

    void prepare_output(const std::string& output_filename)
    {

      chk(
        avformat_alloc_output_context2(&ofmt_ctx_, NULL, NULL, output_filename.c_str()),
        "output avformat_alloc_output_context2 : %s", output_filename.c_str()
      );
      chk(
        ofmt_ctx_,
        "output format context : %s", output_filename.c_str()
      );

      for (unsigned int i = 0; i < ifmt_ctx_->nb_streams; i++) {

        AVStream* out_stream = avformat_new_stream(ofmt_ctx_, NULL);
        chk(
          out_stream,
          "output avformat_new_stream[stream: %u]",
          i
        );

        AVStream* in_stream = ifmt_ctx_->streams[i];
        AVCodecContext* dec_ctx = stream_ctx_[i].dec_;

        if (
          dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
          dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO
        ) {

          //AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
          AVCodec* enc = avcodec_find_encoder(dec_ctx->codec_id);
          chk(
            enc,
            "output avcodec_find_encoder"
          );

          AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
          chk(
            enc_ctx,
            "output avcodec_alloc_context3"
          );

          if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            enc_ctx->height = dec_ctx->height;
            enc_ctx->width = dec_ctx->width;
            enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;

            if (enc->pix_fmts) {
              enc_ctx->pix_fmt = enc->pix_fmts[0];
            } else {
              enc_ctx->pix_fmt = dec_ctx->pix_fmt;
            }
            enc_ctx->time_base = av_inv_q(dec_ctx->framerate);

          } else {
            enc_ctx->sample_rate = dec_ctx->sample_rate;
            if (dec_ctx->channels && !dec_ctx->channel_layout) {
              enc_ctx->channels = dec_ctx->channels;
              enc_ctx->channel_layout = av_get_default_channel_layout(enc_ctx->channels);
            } else if (dec_ctx->channel_layout) {
              enc_ctx->channels = av_get_channel_layout_nb_channels(dec_ctx->channel_layout);
              enc_ctx->channel_layout = dec_ctx->channel_layout;
            } else {
              enc_ctx->channel_layout = AV_CH_LAYOUT_MONO;
              enc_ctx->channels = 1;
            }
            enc_ctx->sample_fmt = enc->sample_fmts[0];
            enc_ctx->time_base.num = 1;
            enc_ctx->time_base.den = enc_ctx->sample_rate;
          }

          chk(
            avcodec_open2(enc_ctx, enc, NULL),
            "output avcodec_open2"
          );

          chk(
            avcodec_parameters_from_context(out_stream->codecpar, enc_ctx),
            "output avcodec_parameters_from_context"
          );

          if (ofmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
          }
          out_stream->time_base = enc_ctx->time_base;
          stream_ctx_[i].enc_ = enc_ctx;

        } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
          chk(AVERROR_INVALIDDATA, "dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN");
        } else {
          // remux
          chk(
            avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar),
            "output avcodec_parameters_copy"
          );
          out_stream->time_base = in_stream->time_base;
        }
      }
      //av_dump_format(ofmt_ctx_, 0, output_filename.c_str(), 1);

      if (!(ofmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        chk(
          avio_open(&ofmt_ctx_->pb, output_filename.c_str(), AVIO_FLAG_WRITE),
          "output avio_open : %s", output_filename.c_str()
        );
      }

      // init muxer, write output file header
      chk(
        avformat_write_header(ofmt_ctx_, NULL),
        "output avformat_write_header"
      );
    }

    void prepare_filter()
    {
      filter_ctx_ = (ff::FilteringContext*)av_malloc_array(ifmt_ctx_->nb_streams, sizeof(*filter_ctx_));
      chk(filter_ctx_, "filter av_malloc_array");


      for (unsigned int i = 0; i < ifmt_ctx_->nb_streams; i++) {
        filter_ctx_[i].buffersrc_ctx = nullptr;
        filter_ctx_[i].buffersink_ctx = nullptr;
        filter_ctx_[i].filter_graph = nullptr;

        AVMediaType codec_type = ifmt_ctx_->streams[i]->codecpar->codec_type;

        if ( codec_type != AVMEDIA_TYPE_AUDIO
          && codec_type != AVMEDIA_TYPE_VIDEO)
        {
          continue;
        }

        //passthrough (dummy) filter
        const char* filter_spec = nullptr;
        if (codec_type == AVMEDIA_TYPE_VIDEO) {
          filter_spec = "null";
        } else {
          filter_spec = "anull";
        }

        prepare_filter(
          &filter_ctx_[i],
          stream_ctx_[i].dec_, // ifmt_ctx_->streams[i]->codec
          stream_ctx_[i].enc_, // ofmt_ctx_->streams[i]->codec
          filter_spec
        );
      }
    }
    
    void prepare_filter(
      ff::FilteringContext* fctx,
      AVCodecContext* dec_ctx,
      AVCodecContext* enc_ctx,
      const char* filter_spec)
    {
      AVFilterContext* buffersrc_ctx = nullptr;
      AVFilterContext* buffersink_ctx = nullptr;

      AVFilterInOut* outputs = avfilter_inout_alloc();
      AVFilterInOut* inputs = avfilter_inout_alloc();
      AVFilterGraph* filter_graph = avfilter_graph_alloc();

      try {
        chk(outputs, "filter video outputs avfilter_inout_alloc");
        chk(inputs, "filter video inputs avfilter_inout_alloc");
        chk(filter_graph, "filter video filter_graph avfilter_graph_alloc");

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
          AVFilter* buffersrc = avfilter_get_by_name("buffer");
          chk(buffersrc, "filter video buffer avfilter_get_by_name");

          AVFilter* buffersink = avfilter_get_by_name("buffersink");
          chk(buffersink, "filter video buffersink avfilter_get_by_name");

          char args[512] = { 0, };
          snprintf(
            args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            dec_ctx->time_base.num, dec_ctx->time_base.den,
            dec_ctx->sample_aspect_ratio.num,
            dec_ctx->sample_aspect_ratio.den
          );

          chk(
            avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph),
            "filter video in avfilter_graph_create_filter"
          );

          chk(
            avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph),
            "filter video out avfilter_graph_create_filter"
          );


          //warning message occurred
          //[swscaler @ 0000026336eaa520] deprecated pixel format used, make sure you did set range correctly
          //switch (enc_ctx->pix_fmt) {
          //case AV_PIX_FMT_YUVJ420P: enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P; break;
          //case AV_PIX_FMT_YUVJ422P: enc_ctx->pix_fmt = AV_PIX_FMT_YUV422P; break;
          //case AV_PIX_FMT_YUVJ444P: enc_ctx->pix_fmt = AV_PIX_FMT_YUV444P; break;
          //case AV_PIX_FMT_YUVJ440P: enc_ctx->pix_fmt = AV_PIX_FMT_YUV440P; break;
          //default: break;
          //}

          chk(
            av_opt_set_bin(buffersink_ctx, "pix_fmts", (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt), AV_OPT_SEARCH_CHILDREN),
            "filter video pix_fmts av_opt_set_bin "
          );

        } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {

          AVFilter* buffersrc = avfilter_get_by_name("abuffer");
          chk(buffersrc, "filter audio abuffer avfilter_get_by_name");

          AVFilter* buffersink = avfilter_get_by_name("abuffersink");
          chk(buffersink, "filter audio abuffersink avfilter_get_by_name");


          if (!dec_ctx->channel_layout) {
            dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
          }

          char args[512] = { 0, };
          snprintf(
            args, sizeof(args),
            "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%llu",
            dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
            av_get_sample_fmt_name(dec_ctx->sample_fmt),
            dec_ctx->channel_layout
          );

          chk(
            avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph),
            "filter audio in avfilter_graph_create_filter"
          );

          chk(
            avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph),
            "filter audio out avfilter_graph_create_filter"
          );

          chk(
            av_opt_set_bin(buffersink_ctx, "sample_fmts", (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt), AV_OPT_SEARCH_CHILDREN),
            "filter audio sample_fmts av_opt_set_bin"
          );

          chk(
            av_opt_set_bin(buffersink_ctx, "channel_layouts", (uint8_t*)&enc_ctx->channel_layout, sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN),
            "filter audio channel_layouts av_opt_set_bin"
          );

          chk(
            av_opt_set_bin(buffersink_ctx, "sample_rates", (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate), AV_OPT_SEARCH_CHILDREN),
            "filter audio sample_rates av_opt_set_bin"
          );
        } else {
          chk(AVERROR_UNKNOWN, "filter audio unknwon");
        }

        /* Endpoints for the filter graph. */
        outputs->name = av_strdup("in");
        outputs->filter_ctx = buffersrc_ctx;
        outputs->pad_idx = 0;
        outputs->next = NULL;
        chk(outputs->name, "filter audio outputs name");

        inputs->name = av_strdup("out");
        inputs->filter_ctx = buffersink_ctx;
        inputs->pad_idx = 0;
        inputs->next = NULL;
        chk(inputs->name, "filter audio inputs name");

        chk(
          avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs, &outputs, NULL),
          "filter audio avfilter_graph_parse_ptr"
        );

        chk(
          avfilter_graph_config(filter_graph, NULL),
          "filter audio avfilter_graph_config"
        );

        /* Fill FilteringContext */
        fctx->buffersrc_ctx = buffersrc_ctx;
        fctx->buffersink_ctx = buffersink_ctx;
        fctx->filter_graph = filter_graph;

      } catch (std::runtime_error& e) {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        throw e;
      }
    }


    void filter_encode_write_frame(ff::Frame& frame, unsigned int stream_index)
    {
      chk(
        av_buffersrc_add_frame_flags(filter_ctx_[stream_index].buffersrc_ctx, frame, 0),
        "av_buffersrc_add_frame_flags"
      );

      // pull filtered frames from the filtergraph
      while (true) {
        ff::Frame filt_frame;
        int ret = av_buffersink_get_frame(filter_ctx_[stream_index].buffersink_ctx, filt_frame);
        if (ret < 0) {
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 0;
          }
          chk(ret, "av_buffersink_get_frame");
          return;
        }

        filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
        encode_write_frame(filt_frame, stream_index);
      }

    }

    void encode_write_frame(ff::Frame& filt_frame, unsigned int stream_index) {
      // encode filtered frame
      AVPacket enc_pkt;
      enc_pkt.data = NULL;
      enc_pkt.size = 0;
      av_init_packet(&enc_pkt);

      chk(avcodec_send_frame(stream_ctx_[stream_index].enc_, filt_frame), "out avcodec_send_frame");

      int ret = 0;
      while (ret >= 0) {
        // encode
        ret = avcodec_receive_packet(stream_ctx_[stream_index].enc_, &enc_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          return;
        }
        chk(ret, "out avcodec_receive_packet");

        // prepare packet for muxing
        enc_pkt.stream_index = stream_index;
        av_packet_rescale_ts(
          &enc_pkt,
          stream_ctx_[stream_index].enc_->time_base,
          ofmt_ctx_->streams[stream_index]->time_base
        );

        // mux encoded frame
        chk(
          av_interleaved_write_frame(ofmt_ctx_, &enc_pkt),
          "av_interleaved_write_frame"
        );
      }
    }

  };
}

