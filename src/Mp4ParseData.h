
#ifndef _DATA_SHARE_H_
#define _DATA_SHARE_H_

#include <map>

#include "ImGuiTools.h"
#include "Myffmpeg.h"
#include "Mp4Parse.h"
#include "imgui.h"
#include "myThread.h"

struct BoxInfo
{
    int                         layer     = 0;
    int                         box_index = 0;
    std::string                 box_type;
    std::shared_ptr<Mp4BoxData> pdata = nullptr;

    // Position In File
    ImS64 boxPosition = 0;
    ImS64 boxSize     = 0;

    std::unique_ptr<uint8_t[]> buffer = nullptr;

#define MAX_BUFFERED_SIZE (1024 * 1024)
    ImS64 bufferSize     = 0;
    ImS64 bufferedOffset = 0;

    std::vector<std::shared_ptr<BoxInfo>> sub_list; // to make sure sub box address will not change
    enum
    {
        CLOSED,
        OPENED,
        FORCE_CLOSE,
        FORCE_OPEN,
    } open_state;

    std::map<std::string, std::shared_ptr<ImGuiBinaryViewer>> binaryValueViewers;
};

enum PARSE_OPERATION_E
{
    OPERATION_PARSE_FILE,
    OPERATION_PARSE_FRAME_TYPE,
    OPERATION_DECODE_FRAME,
};
class Mp4ParseData : public MyThread
{
public:
    Mp4ParseData() {}
    void                       init(const std::function<void(MP4_LOG_LEVEL_E level, const char *str)> &logCallback);
    std::shared_ptr<Mp4Parser> getParser() { return mParser; }
    int                        startParse(PARSE_OPERATION_E op);
    PARSE_OPERATION_E          getCurrentOperation() const { return operation; }
    int   decodeFrameAt(uint32_t trackIdx, uint32_t frameIdx, MyAVFrame &frame, const std::vector<AVPixelFormat> &acceptFormats);
    void  updateData();
    float getParseFileProgress();
    float getParseFrameTypeProgress();
    void  recreateDecoder();

private:
    virtual void run() override;
    virtual void starting() override;
    virtual void stopping() override;

public:
    std::string toParseFilePath;
    std::string curFilePath;
    bool        dataAvailable    = false;
    bool        newDataAvailable = false;

    std::map<int /* trackIdx */, MyAVCodecContext> mVideoDecoders;
    MySwsContext                                   mFmtTransition;
    std::vector<uint64_t>                          mTracksMaxSampleSize;
    std::vector<uint32_t>                          mVideoTracksIdx;

    std::vector<Mp4TrackInfo> tracksInfo;

    std::function<void(unsigned int track_id, int frame_idx, H26X_FRAME_TYPE_E frame_type)> onFrameParsed;

private:
    PARSE_OPERATION_E          operation;
    std::shared_ptr<Mp4Parser> mParser               = createMp4Parser();
    volatile uint64_t          parsingFrameCount     = 0;
    uint64_t                   mTotalVideoFrameCount = 0;
    volatile bool              isContinue            = false;
};

Mp4ParseData &getMp4DataShare();
#endif