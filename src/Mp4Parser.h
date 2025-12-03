#ifndef _MP4_PARSER_GUI_H_
#define _MP4_PARSER_GUI_H_

#include <vector>

#include "Mp4ParseData.h"
#include "ImGuiApplication.h"
#include "ImGuiItem.h"
#include "ImGuiTools.h"

#include "VideoStreamInfo.h"

#define TABLE_FLAGS                                                                                                        \
    (ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders \
     | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX)

class Mp4ParserApp : public ImGui::ImGuiApplication
{
public:
    Mp4ParserApp();

    ~Mp4ParserApp();

    virtual void transferCmdArgs(std::vector<std::string> &args) override;
    virtual void dropFile(const std::vector<std::string> &filesPath) override;

    virtual void presetInternal() override;
    virtual bool renderUI() override;
    void         exitInternal() override;

protected:
    void initSettingsWindowInternal() override;

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

    int  updateData(int type, size_t trackIdx, size_t itemIdx);
    void reset();

private:
    std::string mToParseFile;

    ImGuiID mDockId      = 0;
    ImGuiID mDockIdLeft  = 0;
    ImGuiID mDockIdRight = 0;

    IImGuiWindow mInfoWindow;

    BoxInfo *mCurrBoxSelect   = nullptr;
    int      mCurrTrackSelect = -1;

    enum
    {
        FOCUS_ON_BOXES  = 0,
        FOCUS_ON_TRACKS = 1,
    } mFocusOn = FOCUS_ON_BOXES;

    bool mFocusChanged = false;

    int  mBoxCount           = 0;
    bool mSomeTreeNodeOpened = false;

    std::shared_ptr<BoxInfo> mVirtFileBox;
    std::vector<BoxInfo *>   mAllBoxes;

    // for table, we use map to store them instead of extract data every time render
    std::map<const Mp4BoxData *, ImGui::ImGuiItemTable> mBoxInfoTables;
    std::vector<ImGui::ImGuiItemTable>                  mSampleDataTables;
    std::vector<ImGui::ImGuiItemTable>                  mChunkDataTables;
    VideoStreamInfo                                     mVideoStreamInfo;

    ImGui::ImGuiBinaryViewer mBoxBinaryViewer = ImGui::ImGuiBinaryViewer("Box Data##Binary", true);
    ImGui::ImGuiBinaryViewer mDataViewer      = ImGui::ImGuiBinaryViewer("Binary Data##Data");
    struct
    {
        std::unique_ptr<uint8_t[]> buffer     = nullptr;
        size_t                     bufferSize = 0;
        size_t                     dataSize   = 0;
        int                        type; // 0: chunk, 1: sample
        size_t                     trackIdx;
        size_t                     itemIdx; //  sample or chunk index
    } mBinaryData;

    std::vector<std::pair<int, std::string>> mHWTypeItems = {
        {-1, "Off" },
        {0,  "Auto"},
    };
    bool           mIsHWTypesChecked = false;
    AVHWDeviceType mHWTypesChecked   = AV_HWDEVICE_TYPE_NONE;

    friend class Mp4ParserMainWindow;
};

bool isItemClicked(IMGUI_MOUSE_BUTTON *button = nullptr);

#endif