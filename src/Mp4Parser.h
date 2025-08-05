#ifndef _MP4_PARSER_GUI_H_
#define _MP4_PARSER_GUI_H_

#include <vector>

#include "Mp4ParseData.h"
#include "ImGuiApplication.h"
#include "ImGuiItem.h"
#include "ImGuiTools.h"

#define MYFFMPEG_DEBUG
#include "Myffmpeg.h"

#define TABLE_FLAGS                                                                                                        \
    (ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders \
     | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX)

#define MAX_VIDEO_FRAMES  (180000)
#define HIST_PAGE_SAMPLES (200)
#define MAX_HIST_PAGES    ((MAX_VIDEO_FRAMES) / (HIST_PAGE_SAMPLES))
#define HIST_W_MAX_SCALE  (4.f)
#define HIST_W_MIN_SCALE  (0.125f)
#define HIST_H_MAX_SCALE  (2.f)
#define HIST_H_MIN_SCALE  (0.4f)
#define HIST_WIDTH        (50)
#define HIST_HEIGHT       (360)
#define HIST_BORDER_WIDTH (5)

#define BUTTON_W (15)
#define BUTTON_H (15)

#define ADD_LOG(fmt, ...)                               \
    do                                                  \
    {                                                   \
        char buf[1024] = {0};                           \
        snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
        gUserApp->addLog(buf);                          \
    } while (0)

class VideoStreamInfo
{
public:
    VideoStreamInfo();
    virtual ~VideoStreamInfo();
    bool show();
    void resetData();
    void updateFrameTexture();
    void updateFrameInfo(unsigned int trackIdx, uint32_t frameIdx, H26X_FRAME_TYPE_E frameType);

private:
    void updateData();
    bool show_hist();
    void updateFrameInfo(MyAVFrame &frame);
    void showFrameInfo();

private:
    std::map<unsigned int /* trackIdx */, uint32_t> mCurSelectFrame;

    unsigned int mCurSelectTrack       = 0;
    uint64_t     mHistogramMaxSize     = 0;
    float        mHistogramWidthScale  = 1;
    float        mHistogramHeightScale = 1;

    ImVec2 mWinSize;
    ImVec2 mWinPos;

    ImVec2   mSideBarPos;
    float    mSideBarWidth    = BUTTON_W;
    float    mBottomBarHeight = BUTTON_H;
    ImVec2   mButtonSize;
    ImVec2   mHistogramPos;
    ImVec2   mHistogramSize;
    ImS64    mHistogramScrollPos = 0;
    uint32_t mHistogramStartIdx  = 0;
    uint32_t mHistogramEndIdx    = 0;

    uint32_t mTotalVideoFrameCount = 0;

    ImGuiID     mDockId = 0;
    TextureData mFrameTexture;
    struct FrameInfo
    {
        std::string frameType;
        uint64_t    frameOffset;
        uint64_t    frameSize;
    } mCurrentFrameInfo;

    // Items
    ImageWindow mImageDisplay = ImageWindow("Frame", false);

    ImGuiButton mHeightScaleUpButton    = ImGuiButton("+##height scale");
    ImGuiButton mHeightScaleDownButton  = ImGuiButton("-##height scale");
    ImGuiButton mHeightScaleResetButton = ImGuiButton("o##height scale");
    ImGuiButton mWidthScaleUpButton     = ImGuiButton("+##width scale");
    ImGuiButton mWidthScaleDownButton   = ImGuiButton("-##width scale");
    ImGuiButton mWidthScaleResetButton  = ImGuiButton("o##width scale");
    ImGuiButton mHistMoveLeftButton     = ImGuiButton("<");
    ImGuiButton mHistMoveRightButton    = ImGuiButton(">");
};

class Mp4ParserApp : public ImGuiApplication
{
public:
    Mp4ParserApp();

    ~Mp4ParserApp();

    virtual void transferCmdArgs(std::vector<std::string> &args) override;
    virtual void dropFile(const std::vector<std::string> &filesPath) override;

    virtual void presetInternal() override;
    virtual bool renderUI() override;
    void         exit() override;

private:
    void startParseFile(const std::string &file);
    void resetFileInfo();

    void showMp4InfoTab();

    void ShowTreeNode(BoxInfo *cur_box);
    void ShowBoxesTreeView();
    void ShowTracksTreeView();
    void set_all_open_state(BoxInfo *pBox, bool isClose);

    void ShowInfoItem(uint64_t boxIdx, const std::string &key, const Mp4BoxData &value);
    void ShowInfoView();
    void WrapDatacheckBox();

    std::shared_ptr<BoxInfo> getBoxInfo(const Mp4Box *pBox, int layer, int &boxCount);
    void                     updateSamplesTable();
    void                     updateChunksTable();

    void updateBoxDataTables(const BoxInfo &pBox);
    void updateBoxDataTables(uint64_t boxIdx, const std::string &key, const Mp4BoxData &value);

    void sampleTableClicked(size_t trackIdx, size_t rowIdx, size_t colIdx);
    void chunkTableClicked(size_t trackIdx, size_t rowIdx, size_t colIdx);
    void saveCurrentData(const std::string &fileName, size_t size);

    int updateData(int type, size_t trackIdx, size_t itemIdx);

private:
    std::string mToParseFile;

    ImGuiID mDockId = 0;

    bool m_metrics_show = false;

    IImGuiWindow mInfoWindow;

    BoxInfo *cur_box_select   = nullptr;
    int      cur_track_select = -1;

    enum
    {
        FOCUS_ON_BOXES  = 0,
        FOCUS_ON_TRACKS = 1,
    } m_focus_on = FOCUS_ON_BOXES;

    bool m_focus_changed = false;

    int  m_box_num           = 0;
    bool mSomeTreeNodeOpened = false;

    std::shared_ptr<BoxInfo> mVirtFileBox;
    std::vector<BoxInfo *>   mAllBoxes;

    // for table, we use map to store them instead of extract data every time render
    std::map<const Mp4BoxData *, ImGuiItemTable> mBoxInfoTables;
    std::vector<ImGuiItemTable>                  mSampleDataTables;
    std::vector<ImGuiItemTable>                  mChunkDataTables;
    VideoStreamInfo                              mVideoStreamInfo;

    ImGuiBinaryViewer mBoxBinaryViewer = ImGuiBinaryViewer("Box Data##Binary", true);
    ImGuiBinaryViewer mDataViewer      = ImGuiBinaryViewer("Binary Data##Data");
    struct
    {
        std::unique_ptr<uint8_t[]> buffer     = nullptr;
        size_t                     bufferSize = 0;
        size_t                     dataSize   = 0;
        int                        type; // 0: chunk, 1: sample
        size_t                     trackIdx;
        size_t                     itemIdx; //  sample or chunk index
    } mBinaryData;

    friend class Mp4ParserMainWindow;
};

bool isItemClicked(IMGUI_MOUSE_BUTTON *button = nullptr);

#endif