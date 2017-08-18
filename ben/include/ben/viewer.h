#pragma once

#include "ffmpeg.h"
#include "opencv.h"


namespace ben {

  class Viewer : public ff::Util
  {
  private:
    uint8_t* buffer_ = nullptr;
    ff::Frame frame_rgb_;

  public:
    Viewer() {}

    ~Viewer()
    {
      if (buffer_) {
        av_free(buffer_);
        buffer_ = nullptr;
      }
    }


    void init(AVCodecContext* dec_ctx)
    {
      AVPixelFormat pixel_format = AV_PIX_FMT_BGR24;

      int bytes = av_image_get_buffer_size(
        pixel_format,
        dec_ctx->width,
        dec_ctx->height,
        1
      );

      buffer_ = (uint8_t*)av_malloc(bytes * sizeof(uint8_t));

      chk(
        av_image_fill_arrays(
          frame_rgb_->data,
          frame_rgb_->linesize,
          buffer_,
          pixel_format,
          dec_ctx->width,
          dec_ctx->height,
          1),
        "viewer av_image_fill_arrays"
      );
    }

    void view(AVCodecContext* dec_ctx, ff::Frame& frame)
    {
      AVPixelFormat pix_fmt = dec_ctx->pix_fmt;
      switch (pix_fmt) {
      case AV_PIX_FMT_YUVJ420P: pix_fmt = AV_PIX_FMT_YUV420P; break;
      case AV_PIX_FMT_YUVJ422P: pix_fmt = AV_PIX_FMT_YUV422P; break;
      case AV_PIX_FMT_YUVJ444P: pix_fmt = AV_PIX_FMT_YUV444P; break;
      case AV_PIX_FMT_YUVJ440P: pix_fmt = AV_PIX_FMT_YUV440P; break;
      default: break;
      }

      struct SwsContext* img_convert_ctx = sws_getCachedContext(
        NULL,
        dec_ctx->width,
        dec_ctx->height,
        pix_fmt,
        dec_ctx->width,
        dec_ctx->height,
        AV_PIX_FMT_BGR24,
        SWS_BICUBIC, NULL, NULL, NULL
      );
      chk(img_convert_ctx, "viewer sws_getCachedContext");

      sws_scale(
        img_convert_ctx,
        frame->data,
        frame->linesize,
        0,
        dec_ctx->height,
        frame_rgb_->data,
        frame_rgb_->linesize
      );

      //OpenCV
      cv::Mat img(
        frame->height,
        frame->width,
        CV_8UC3,
        frame_rgb_->data[0]
      );
      cv::imshow("display", img);
      cvWaitKey(1);

      sws_freeContext(img_convert_ctx);
    }

  private:
    void close()
    {
      av_free(buffer_);
    }

  };
}