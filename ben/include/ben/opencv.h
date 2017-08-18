#pragma once

#pragma warning(disable: 4819)
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#if defined(_DEBUG)
#pragma comment (lib, "opencv_world330d.lib")
#else
#pragma comment (lib, "opencv_world330.lib")
#endif
#pragma warning(default: 4819)
