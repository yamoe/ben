#pragma once

#include <conio.h>
#include <stdexcept>

extern "C" {
  #include <libavcodec/avcodec.h>
  #pragma comment (lib, "avcodec.lib")

  #include <libavdevice/avdevice.h>
  #pragma comment (lib, "avdevice.lib")

  #include <libavfilter/avfilter.h>
  #pragma comment (lib, "avfilter.lib")

  #include <libavformat/avformat.h>
  #pragma comment (lib, "avformat.lib")

  #include <libavutil/avutil.h>
  #include <libavutil/imgutils.h>
  #pragma comment (lib, "avutil.lib")

  #include <libpostproc/postprocess.h>
  #pragma comment (lib, "postproc.lib")

  #include <libswresample/swresample.h>
  #pragma comment (lib, "swresample.lib")

  #include <libswscale/swscale.h>
  #pragma comment (lib, "swscale.lib")

  #include <libavfilter/avfiltergraph.h>
  #include <libavfilter/buffersink.h>
  #include <libavfilter/buffersrc.h>
  #include <libavutil/opt.h>
  #include <libavutil/pixdesc.h>
}

namespace ben {
  namespace ff {

    class StreamContext
    {
    public:
      AVCodecContext* dec_ = nullptr;
      AVCodecContext* enc_ = nullptr;
    };

    class FilteringContext
    {
    public:
      AVFilterContext* buffersink_ctx;
      AVFilterContext* buffersrc_ctx;
      AVFilterGraph* filter_graph;
    };

    class Frame
    {
    private:
      AVFrame* frame_ = nullptr;

    public:
      Frame(bool alloc = true)
      {
        if (alloc) {
          frame_ = av_frame_alloc();
        }
      }

      ~Frame()
      {
        if (frame_) {
          av_frame_free(&frame_);
          frame_ = nullptr;
        }
      }

      operator AVFrame*()
      {
        return frame_;
      }

      AVFrame* operator->()
      {
        return frame_;
      }

    };

    class Packet
    {
    private:
      AVPacket packet_;

    public:
      Packet()
      {
        packet_.data = nullptr;
        packet_.size = 0;
        av_init_packet(&packet_);
      }

      ~Packet()
      {
        av_packet_unref(&packet_);
      }

      operator AVPacket*()
      {
        return &packet_;
      }

      AVPacket* operator->()
      {
        return &packet_;
      }

    };


    class Util
    {
    public:
      template <std::size_t size = 1024>
      static void chk(int ret, const char* fmt, ...)
      {
        if (ret < 0) {
          char av_err_str[AV_ERROR_MAX_STRING_SIZE] = { 0, };
          av_make_error_string(av_err_str, AV_ERROR_MAX_STRING_SIZE, ret);

          char buf[size] = { 0, };
          int writed = sprintf_s(buf, "av fail (%d:%s). ", ret, av_err_str);

          char* buf2 = buf + writed;
          va_list list;
          va_start(list, fmt);
          vsnprintf_s(buf2, size - writed, size - writed - 1, fmt, list);
          va_end(list);

          throw std::runtime_error(buf);
        }
      }

      template <std::size_t size = 1024>
      static void chk(void* ret, const char* fmt, ...)
      {
        if (!ret) {
          char buf[size] = { 0, };
          int writed = sprintf_s(buf, "av fail. ");

          char* buf2 = buf + writed;
          va_list list;
          va_start(list, fmt);
          vsnprintf_s(buf2, size - writed, size - writed - 1, fmt, list);
          va_end(list);


          throw std::runtime_error(buf);
        }
      }

      static bool chk_exit_key()
      {
        if (_kbhit() > 0) {
          int ch = _getch();

          //ESC : 27, q : 81, Q : 113
          if (ch == 27 || ch == 81 || ch == 113) {
            return true;
          }
        }
        return false;
      }

      static void show_dshow_device() {
        AVFormatContext* fmt_ctx = avformat_alloc_context();
        AVDictionary* opt = NULL;
        av_dict_set(&opt, "list_devices", "true", 0);
        AVInputFormat* in_fmt = av_find_input_format("dshow");

        printf("-- dshow device list --\n");
        avformat_open_input(&fmt_ctx, "video=dummy", in_fmt, &opt);
        printf("-----------------------\n");
        avformat_free_context(fmt_ctx);
      }

      static void show_dshow_device_option(const char* url = "video=USB Video Device") {
        AVFormatContext* fmt_ctx = avformat_alloc_context();
        AVDictionary* opt = NULL;
        av_dict_set(&opt, "list_opt", "true", 0);
        AVInputFormat* in_fmt = av_find_input_format("dshow");

        printf("-- device info : %s --\n", url);
        avformat_open_input(&fmt_ctx, url, in_fmt, &opt);
        printf("-----------------------\n");
        avformat_free_context(fmt_ctx);
      }

      static void show_vfw_device() {
        AVFormatContext* fmt_ctx = avformat_alloc_context();
        AVInputFormat* in_fmt = av_find_input_format("vfwcap");

        printf("-- vfw device list --\n");
        avformat_open_input(&fmt_ctx, "list", in_fmt, NULL);
        printf("-----------------------\n");
        avformat_free_context(fmt_ctx);
      }

      static void show_avfoundation_device() {
        AVFormatContext* fmt_ctx = avformat_alloc_context();
        AVDictionary* opt = NULL;
        av_dict_set(&opt, "list_devices", "true", 0);
        AVInputFormat* in_fmt = av_find_input_format("avfoundation");

        printf("-- avfoundation device list --\n");
        avformat_open_input(&fmt_ctx, "", in_fmt, &opt);
        printf("-----------------------\n");
        avformat_free_context(fmt_ctx);
      }
    };


    // ffmpeg 로그 설정 예
    class Log
    {
    public:
      Log() {}
      ~Log() {}

      static void set_log(int level = AV_LOG_INFO, bool set_callback = false)
      {
        av_log_set_level(level);
        if (set_callback) {
          av_log_set_callback(Log::callback);
        }
      }

      static void callback(void *ptr, int level, const char *fmt, va_list vargs)
      {
        if (level > av_log_get_level()) return;

        AVClass* avc = ptr ? *(AVClass**)ptr : NULL;
        if (avc) {
          printf("[%s @ %p]", avc->item_name(ptr), ptr);
        }

        char msg[1024] = { 0 };
        vsprintf_s(msg, fmt, vargs);
        printf(" %s", msg);
      }

    };

  }

}

