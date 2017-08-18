#pragma once

#include <map>
#include <string>
#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objidl.h>
#include <strmif.h>
#include <dshow.h>
#pragma comment (lib, "strmiids.lib")

namespace ben {
 
  class Devices
  {
  public:
    typedef std::map<uint32_t, std::string> Map;

  private:
    std::string last_err_;
    Map video_;
    Map audio_;

  public:
    Devices() {}
    ~Devices() {}

    Map& video_list()
    {
      return video_;
    }

    Map& audio_list()
    {
      return audio_;
    }

    bool query_video()
    {
      last_err_.clear();
      video_ = query(CLSID_VideoInputDeviceCategory);
      return last_err_.empty();
    }

    bool query_audio()
    {
      last_err_.clear();
      audio_ = query(CLSID_AudioInputDeviceCategory);
      return last_err_.empty();
    }

    std::string& last_err()
    {
      return last_err_;
    }

  private:

    bool succeeded(HRESULT hr, char* msg = "")
    {
      if (FAILED(hr)) {
        char buf[1024] = { 0, };
        sprintf_s(buf, "fail %s [0x%08x]", msg, hr);
        last_err_ = buf;
        return false;
      }
      return true;
    }
    

    Map query(REFGUID category)
    {
      Map m;

      HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
      if (!succeeded(hr, "CoInitializeEx")) {
        return m;
      }

      IEnumMoniker* enum_moniker = nullptr;
      if (create(category, &enum_moniker)) {
        m = query(enum_moniker);
        enum_moniker->Release();
      }

      CoUninitialize();

      return m;
    }

    bool create(REFGUID category, IEnumMoniker** enum_moniker)
    {
      ICreateDevEnum* dev_enum = nullptr;

      HRESULT hr = CoCreateInstance(
        CLSID_SystemDeviceEnum,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dev_enum)
      );
      if (!succeeded(hr, "CoCreateInstance")) {
        return false;
      }

      hr = dev_enum->CreateClassEnumerator(category, enum_moniker, 0);
      dev_enum->Release();
      if (!succeeded(hr, "CreateClassEnumerator")) {
        return false;
      }

      return true;;
    }


    static Map query(IEnumMoniker* enum_moniker)
    {
      uint32_t id = 0;
      Map map;

      IMoniker* monikier = NULL;

      while (enum_moniker->Next(1, &monikier, NULL) == S_OK) {

        IPropertyBag* prop_bag = NULL;
        HRESULT hr = monikier->BindToStorage(0, 0, IID_PPV_ARGS(&prop_bag));
        if (FAILED(hr)) {
          monikier->Release();
          continue;
        }

        VARIANT var;
        VariantInit(&var);

        hr = prop_bag->Read(L"Description", &var, 0);
        if (FAILED(hr)) {
          hr = prop_bag->Read(L"FriendlyName", &var, 0);
        }

        if (SUCCEEDED(hr)) {
          // w_chart -> char
          int len = ::WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, 0, 0, 0, 0);
          std::string out(len, 0);
          ::WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, const_cast<char*>(out.c_str()), len, 0, 0);

          map[id++] = out;
          VariantClear(&var);
        }

        if (prop_bag) {
          prop_bag->Release();
        }
      }
      return map;
    }


  };
}

