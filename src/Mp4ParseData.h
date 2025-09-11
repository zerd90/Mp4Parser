
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

    std::map<std::string, std::shared_ptr<ImGui::ImGuiBinaryViewer>> binaryValueViewers;
};

enum PARSE_OPERATION_E
{
    OPERATION_PARSE_FILE,
    OPERATION_PARSE_FRAME_TYPE,
    OPERATION_DECODE_FRAME,
};

struct FrameCacheData
{
    FrameCacheData();
    virtual ~FrameCacheData();
    FrameCacheData(FrameCacheData &&)            = default;
    FrameCacheData &operator=(FrameCacheData &&) = default;

    std::unique_ptr<uint8_t[]> jpegData;
    uint32_t                   jpegSize = 0;
    uint32_t                   width    = 0;
    uint32_t                   height   = 0;
    uint32_t                   ptsMs    = 0;
};

class Mp4ParseData : public MyThread
{
public:
    Mp4ParseData() {}
    void                       init(const std::function<void(MP4_LOG_LEVEL_E level, const char *str)> &logCallback);
    std::shared_ptr<Mp4Parser> getParser() { return mParser; }
    int                        startParse(PARSE_OPERATION_E op);
    PARSE_OPERATION_E          getCurrentOperation() const { return mOperation; }
    void                       updateData();
    float                      getParseFileProgress();
    float                      getParseFrameTypeProgress();
    void                       recreateDecoder();
    void                       clear();
    void                       clearData();

    int decodeFrameAt(uint32_t trackIdx, uint32_t frameIdx, MyAVFrame &frame, const std::vector<AVPixelFormat> &acceptFormats);
    // 0 - seek to key frame before; 1 - frame in cache; 2 - continue decode to the frame; -1 - seek fail
    int seekToFrame(uint32_t trackIdx, uint32_t frameIdx, uint32_t &keyFrameIdx);

    int saveFrameToFile(uint32_t trackIdx, uint32_t frameIdx);

private:
    virtual void run() override;
    virtual void starting() override;
    virtual void stopping() override;

    int sendPacketToDecoder(uint32_t trackIdx, uint32_t frameIdx);
    int decodeOneFrame(uint32_t trackIdx, MyAVFrame &frame);
    int transformFrameFormat(MyAVFrame &frame, const std::vector<AVPixelFormat> &acceptFormats);

    std::unique_ptr<uint8_t[]> encodeFrameToJpeg(MyAVFrame &frame, uint32_t &jpegSize);
    int                        decodeJpegToFrame(uint8_t *jpegData, uint32_t jpegSize, MyAVFrame &frame);
    void                       addFrameToCache(MyAVFrame &frame);

public:
    std::string toParseFilePath;
    std::string curFilePath;
    bool        dataAvailable = false;

    std::vector<uint64_t> tracksMaxSampleSize;
    std::vector<uint32_t> videoTracksIdx;

    std::vector<Mp4TrackInfo> tracksInfo;

    std::function<void(unsigned int track_id, int frame_idx, H26X_FRAME_TYPE_E frame_type)> onFrameParsed;

    std::map<int /* trackIdx */, std::vector<uint32_t>> tracksFramePtsList; // sort by pts

private:
    PARSE_OPERATION_E          mOperation = OPERATION_PARSE_FILE;
    std::shared_ptr<Mp4Parser> mParser    = createMp4Parser();

    std::map<int /* trackIdx */, MyAVCodecContext> mVideoDecoders;
    MySwsContext                                   mFmtTransition;

    volatile uint64_t mParsingFrameCount    = 0;
    uint64_t          mTotalVideoFrameCount = 0;
    volatile bool     mIsContinue           = false;

    struct TrackDecodeInfo
    {
        int64_t lastDecodedFrameIdx = -1;
        int64_t lastExtractFrameIdx = -1; // if there's B Frame, lastExtractFrameIdx may not equal to lastDecodedFrameIdx
    };
    std::map<int /* trackIdx */, TrackDecodeInfo> mTracksDecodeStat;

    std::vector<FrameCacheData> mDecodeFrameCache; // cache decoded frames as jpeg
};

Mp4ParseData &getMp4DataShare();
#endif