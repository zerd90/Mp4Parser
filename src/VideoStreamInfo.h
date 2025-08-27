#ifndef _VIDEO_STREAM_INFO_H_
#define _VIDEO_STREAM_INFO_H_

#include <functional>
#include <map>

#include "ImGuiTools.h"
#include "ImGuiWindow.h"
#include "Mp4Types.h"
#include "imgui.h"
#include "imgui_common_tools.h"

#define MYFFMPEG_DEBUG
#include "Myffmpeg.h"

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

class PlayProgressBar
{
public:
    PlayProgressBar();

    virtual ~PlayProgressBar();
    void setCallbacks(std::function<void(float progress)> onProgress, std::function<float()> getProgress);
    void show();

private:
    std::function<void(float progress)> mOnProgress;
    std::function<float()>              mGetProgress;
    float                               mProgress = 0;
};

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
    bool drawHistogram(bool updateScroll);
    void updateFrameInfo(MyAVFrame &frame);
    void showFrameInfo();
    void showFrameDisplay();
    bool showHistogramAndFrameInfo(bool updateScroll);

private:
    std::map<unsigned int /* trackIdx */, uint32_t /* frameIdx sort by pts */> mCurSelectFrame;

    bool mSelectChanged = false;

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

    ImGui::TextureData mFrameTexture;
    struct FrameInfo
    {
        uint32_t    frameIdx;
        std::string frameType;
        uint64_t    frameOffset;
        uint64_t    frameSize;
        uint64_t    dtsMs;
        uint64_t    ptsMs;
    } mCurrentFrameInfo;

    // Items
    ImGui::ImageWindow       mImageDisplay = ImGui::ImageWindow("Frame Render", true);
    ImGui::IImGuiChildWindow mFrameDisplay = ImGui::IImGuiChildWindow("Frame Display");

    ImGui::ImGuiButton mHeightScaleUpButton    = ImGui::ImGuiButton("+##height scale");
    ImGui::ImGuiButton mHeightScaleDownButton  = ImGui::ImGuiButton("-##height scale");
    ImGui::ImGuiButton mHeightScaleResetButton = ImGui::ImGuiButton("o##height scale");
    ImGui::ImGuiButton mWidthScaleUpButton     = ImGui::ImGuiButton("+##width scale");
    ImGui::ImGuiButton mWidthScaleDownButton   = ImGui::ImGuiButton("-##width scale");
    ImGui::ImGuiButton mWidthScaleResetButton  = ImGui::ImGuiButton("o##width scale");
    ImGui::ImGuiButton mHistMoveLeftButton     = ImGui::ImGuiButton("<");
    ImGui::ImGuiButton mHistMoveRightButton    = ImGui::ImGuiButton(">");

    ImGui::ImGuiButton mNextFrameButton = ImGui::ImGuiButton(">##next frame");
    ImGui::ImGuiButton mPrevFrameButton = ImGui::ImGuiButton("<##prev frame");
    ImGui::ImGuiButton mPlayButton      = ImGui::ImGuiButton("Play##button");
    ImGui::ImGuiButton mPauseButton     = ImGui::ImGuiButton("Pause##button");

    ImGui::ImGuiInputCombo mFrameRateCombo = ImGui::ImGuiInputCombo("Framerate");

    bool     mIsPlaying      = false;
    uint64_t mLastPlayTimeMs = 0;
    uint32_t mPlayIntervalMs = 50; // 20fps
    ImVec2   mPlayControlPanelSize;

    uint64_t       mLastMoveLeftTime  = 0;
    uint64_t       mLastMoveRightTime = 0;
    const uint64_t mMoveInterval      = 50;

    PlayProgressBar mPlayProgressBar;
};

#endif