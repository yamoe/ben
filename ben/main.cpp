
#include "stdafx.h"

#include <ben/devices.h>
#include <ben/webcam.h>

#include "example_show_webcam.h"

int main(int argc, const char ** argv)
{
  //return example_show_webcam();


  ben::Devices devices;
  printf("--video list------------\n");
  if (devices.query_video()) {
    for (auto it : devices.video_list()) {
      printf("id : %u, name : %s\n", it.first, it.second.c_str());
    }
  } else {
    printf("fail to get video list : %s\n", devices.last_err().c_str());
  }

  printf("--audio list------------\n");
  if (devices.query_audio()) {
    for (auto it : devices.audio_list()) {
      printf("id : %u, name : %s\n", it.first, it.second.c_str());
    }
  } else {
    printf("fail to get audio list : %s\n", devices.last_err().c_str());
  }



  

  printf("--start capture------------\n");
  ben::ff::Log::set_log();
  ben::Webcam wc;
  if (!wc.start_capture(
    "USB Video Device",
    "",
    "C:\\Users\\ky\\Downloads\\ben\\ben.mp4"))
  {
    printf("fail start capture : %s\n", wc.last_err().c_str());
    return -1;
  }

  while (true) {
    if (!wc.capturing()) {
      printf("fail capturing : %s\n", wc.last_err().c_str());
      break;
    }

    // key : q, Q, ESC
    if (ben::ff::Util::chk_exit_key()) {
      wc.end_capture();
      break;
    }
  }

  printf("\n\nexit...\n");

  return 0;
}

