#ifndef _APP_CONFIGURE_H_
#define _APP_CONFIGURE_H_

#include <string>

class AppConfigures
{
public:
    bool needShowInHex    = true; // show size/offset in hex
    bool logarithmicAxis  = false;
    int  hardwareDecode   = -1; // -1 - off; 0 - auto; >0 - enum AVHWDeviceType
    bool needShowDebugLog = false;
    bool showWrappedData  = false; // show wrapped data in binary viewer, append SPS/PPS/VPS, transform start code
    bool showRawFrameType = false; // show raw frame type in frame table
    int  playFrameRate    = 20;
    int  playIFrameRate   = 5;
    bool showFrameInfo    = true;
    bool loopPlay         = true;

    std::string saveFramePath = "";

    // not saving
    bool showBoxBinaryData = false;
    bool onlyPlayIFrame    = false;
};

AppConfigures &getAppConfigure();

#endif // !_APP_CONFIGURE_H_