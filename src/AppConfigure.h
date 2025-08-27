#ifndef _APP_CONFIGURE_H_
#define _APP_CONFIGURE_H_

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
    bool showFrameInfo    = true;

    // not saving
    bool showBoxBinaryData = false;
};

AppConfigures &getAppConfigure();

#endif // !_APP_CONFIGURE_H_