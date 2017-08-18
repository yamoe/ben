#pragma once

#include <ben/ffmpeg.h>
#include <ben/opencv.h>

int example_show_webcam()
{
  avdevice_register_all();
  avcodec_register_all();

  /*
  find out device name:
  1. vfwcap - 0
  cmd> ffmpeg -y -f vfwcap -i list
  [vfwcap @ 000000000252a500] Driver 0
  [vfwcap @ 000000000252a500]  Microsoft WDM Image Capture (Win32)
  [vfwcap @ 000000000252a500]  Version:  10.0.15063.0

  2. dshow - video=USB Video Device
  cmd> ffmpeg -list_devices true -f dshow -i dummy
  [dshow @ 000000000064a600] DirectShow video devices (some may be both video and audio devices)
  [dshow @ 000000000064a600]  "USB Video Device"
  [dshow @ 000000000064a600]     Alternative name "@device_pnp_\\?\usb#vid_090c&pid_37c0&mi_00#6&2f7352c6&0&0000#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\global"
  [dshow @ 000000000064a600] DirectShow audio devices
  [dshow @ 000000000064a600]  "마이크(High Definition Audio 장치)"
  [dshow @ 000000000064a600]     Alternative name "@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\wave_{8CEE8CA4-D5AD-4E2A-A75E-D89F0F4C1666}"
  [dshow @ 000000000064a600]  "마이크(High Definition Audio 장치)"
  [dshow @ 000000000064a600]     Alternative name "@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\wave_{131F0938-5158-4B95-8B42-3DCF512E28C6}"
  */

  //(dshow, video=USB Video Device) or (vfwcap, 0) 
  std::string input_format_name = "dshow"; // vfwcap
  std::string device_name = "video=USB Video Device";  // 0

  AVInputFormat* input_format = av_find_input_format(input_format_name.c_str());
  AVFormatContext *format_context = avformat_alloc_context();

  AVDictionary* opt = NULL;
  //av_dict_set(&opt, "video_size", "320x240", 0);

  // open
  if (avformat_open_input(&format_context, device_name.c_str(), input_format, &opt) != 0) {
    printf("fail avformat_open_input\n");
    return -1;
  }

  if (avformat_find_stream_info(format_context, NULL) < 0) {
    printf("fail avformat_find_stream_info\n");
    return -1;
  }

  // print information
  av_dump_format(format_context, 0, device_name.c_str(), 0);

  // find stream (video, audio)
  int video_stream = -1;
  int audio_stream = -1;

  for (unsigned int i = 0; i < format_context->nb_streams; i++) {
    AVMediaType codec_type = format_context->streams[i]->codecpar->codec_type;
    if (codec_type == AVMEDIA_TYPE_VIDEO && video_stream == -1) {
      video_stream = i;
    }
    else if (codec_type == AVMEDIA_TYPE_AUDIO && audio_stream == -1) {
      audio_stream = i;
    }
  }

  if (video_stream == -1) {
    printf("fail video stream\n");
    return -1;
  }
  if (audio_stream == -1) {
    printf("info audio stream\n");
  }

  AVCodecParameters* codex_params = format_context->streams[video_stream]->codecpar;
  AVCodec* dec = avcodec_find_decoder(codex_params->codec_id);
  if (dec == NULL) {
    printf("fail avcodec_find_decoder\n");
    return -1;
  }

  AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
  if (dec_ctx == NULL) {
    printf("fail avcodec_alloc_context3\n");
    return -1;
  }
  if (avcodec_parameters_to_context(dec_ctx, codex_params) < 0) {
    printf("fail avcodec_parameters_to_context\n");
    return -1;
  }

  if (avcodec_open2(dec_ctx, dec, NULL) < 0) {
    printf("fail avcodec_open2\n");
    return -1;
  }

  AVFrame* frame = av_frame_alloc();
  AVFrame* frame_rgb = av_frame_alloc();

  AVPixelFormat format = AV_PIX_FMT_BGR24;
  int num_bytes = av_image_get_buffer_size(format, dec_ctx->width, dec_ctx->height, 1);
  uint8_t* buffer = 0;
  buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));

  if (av_image_fill_arrays(frame_rgb->data, frame_rgb->linesize, buffer, format, dec_ctx->width, dec_ctx->height, 1) < 0) {
    printf("fail av_image_fill_arrays\n");
    return -1;
  }

  AVPacket packet;
  int ret = 0;
  int res = 0;

  while (res = av_read_frame(format_context, &packet) >= 0) {

    if (packet.stream_index == video_stream) {
      // decode
      ret = avcodec_send_packet(dec_ctx, &packet);
      if (ret < 0) {
        if (ret == AVERROR_EOF) {
          printf("EOF - avcodec_send_packet\n");
          break;
        }
        printf("fail avcodec_send_packet\n");
        return -1;
      }
      ret = avcodec_receive_frame(dec_ctx, frame);
      if (ret < 0) {
        printf("fail avcodec_receive_frame\n");
        return -1;
      }
      ///////////////////////////////////////

      switch (dec_ctx->pix_fmt) {
      case AV_PIX_FMT_YUVJ420P: dec_ctx->pix_fmt = AV_PIX_FMT_YUV420P; break;
      case AV_PIX_FMT_YUVJ422P: dec_ctx->pix_fmt = AV_PIX_FMT_YUV422P; break;
      case AV_PIX_FMT_YUVJ444P: dec_ctx->pix_fmt = AV_PIX_FMT_YUV444P; break;
      case AV_PIX_FMT_YUVJ440P: dec_ctx->pix_fmt = AV_PIX_FMT_YUV440P; break;
      default: break;
      }

      struct SwsContext* img_convert_ctx = sws_getCachedContext(
        NULL,
        dec_ctx->width,
        dec_ctx->height,
        dec_ctx->pix_fmt,
        dec_ctx->width,
        dec_ctx->height,
        AV_PIX_FMT_BGR24,
        SWS_BICUBIC,
        NULL, NULL, NULL);

      if (img_convert_ctx == NULL) {
        printf("fail sws_getCachedContext\n");
        return -1;
      }

      sws_scale(
        img_convert_ctx,
        frame->data,
        frame->linesize,
        0,
        dec_ctx->height,
        frame_rgb->data,
        frame_rgb->linesize
      );

      //open cv
      cv::Mat img(frame->height, frame->width, CV_8UC3, frame_rgb->data[0]);
      cv::imshow("display", img);
      cvWaitKey(1);

      sws_freeContext(img_convert_ctx);

      av_packet_unref(&packet);


      // escape
      if (_kbhit() > 0) {
        int ch = _getch();

        //ESC key
        if (ch == 27) {
          printf("exit\n");
          break;
        }
      }
    }
  }


  // i did not care .... because this is just test code..;;
  av_packet_unref(&packet);
  avcodec_close(dec_ctx);

  avformat_close_input(&format_context);
  av_free(frame);
  av_free(frame_rgb);
  av_free(buffer);

  return 0;
}
