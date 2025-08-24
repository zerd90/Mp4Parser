
#include "bits.h"
#define IMGUI_DEFINE_MATH_OPERATORS

#include "VideoStreamInfo.h"
#include "Mp4ParseData.h"
#include "AppConfigure.h"
#include "timer.h"

using std::string;
using namespace ImGui;

PlayProgressBar::PlayProgressBar() {}
PlayProgressBar::~PlayProgressBar() {};

void PlayProgressBar::setCallbacks(std::function<void(float progress)> onProgress, std::function<float()> getProgress)
{
    mOnProgress  = onProgress;
    mGetProgress = getProgress;
}

void PlayProgressBar::show()
{
    static const int    sProgressBarHeight = 10;
    static const ImVec2 sBlockSize         = {10, 20};

    if (mGetProgress)
        mProgress = mGetProgress();
    ImVec2 barPos = ImGui::GetCursorScreenPos();

    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y      = sProgressBarHeight;

    // #C5C5C5FF
    ImGui::GetWindowDrawList()->AddRectFilled(barPos, barPos + size, ImColor(197, 197, 197, 255));
    // #4460DD
    ImGui::GetWindowDrawList()->AddRectFilled(barPos, barPos + ImVec2(size.x * mProgress, size.y),
                                              ImColor(0x44, 0x60, 0xdd, 255));

    ImVec2 blockPos = barPos + ImVec2(size.x * mProgress, 0);
    blockPos.x -= sBlockSize.x / 2;
    blockPos.y -= (sBlockSize.y - sProgressBarHeight) / 2;
    ImGui::GetWindowDrawList()->AddRectFilled(blockPos, blockPos + sBlockSize, ImColor(255, 255, 255, 255), 2.f);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        if (mousePos.x >= barPos.x && mousePos.x <= barPos.x + size.x && mousePos.y >= barPos.y
            && mousePos.y <= barPos.y + size.y)
        {
            mProgress = (mousePos.x - barPos.x) / size.x;
            if (mOnProgress)
                mOnProgress(mProgress);
        }
    }

    ImGui::SetCursorScreenPos(barPos + ImVec2(0, sProgressBarHeight + ImGui::GetStyle().ItemSpacing.y));
}

void VideoStreamInfo::updateFrameInfo(MyAVFrame &frame)
{
    IM_UNUSED(frame);
    if (!getMp4DataShare().dataAvailable)
    {
        memset(&mCurrentFrameInfo, 0, sizeof(mCurrentFrameInfo));
        return;
    }
    auto &samples = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;
    auto &ptsList = getMp4DataShare().tracksFramePtsList[mCurSelectTrack];
    if (ptsList.empty())
    {
        memset(&mCurrentFrameInfo, 0, sizeof(mCurrentFrameInfo));
        return;
    }
    auto &sample                  = samples[ptsList[mCurSelectFrame[mCurSelectTrack]]];
    mCurrentFrameInfo.frameIdx    = (uint32_t)sample.sampleIdx;
    mCurrentFrameInfo.frameType   = mp4GetFrameTypeStr(sample.frameType);
    mCurrentFrameInfo.frameOffset = sample.sampleOffset;
    mCurrentFrameInfo.frameSize   = sample.sampleSize;
    mCurrentFrameInfo.dtsMs       = sample.dtsMs;
    mCurrentFrameInfo.ptsMs       = sample.ptsMs;
}

void VideoStreamInfo::updateFrameTexture()
{
    MyAVFrame frame;
    auto     &ptsList      = getMp4DataShare().tracksFramePtsList[mCurSelectTrack];
    uint32_t  realFrameIdx = ptsList[mCurSelectFrame[mCurSelectTrack]];
    if (getMp4DataShare().decodeFrameAt(mCurSelectTrack, realFrameIdx, frame, {AV_PIX_FMT_RGBA}) < 0)
        return;

    updateFrameInfo(frame);

    updateImageTexture(&mFrameTexture, frame->data[0], frame->width, frame->height, frame->linesize[0]);

    mImageDisplay.setTexture(mFrameTexture);
    mFrameDisplay.open();
}

// #FF0000FF
#define I_FRAME_COLOR  (bswap_32(0xFF0000FFu))
// #0032FFFF
#define P_FRAME_COLOR  (bswap_32(0x0032FFFFu))
// #2BBE44FF
#define B_FRAME_COLOR  (bswap_32(0x2BBE44FFu))
// #8065bFFF
#define BORDER_COLOR   (bswap_32(0x8065bFFFu))
// #13082CFF
#define SEL_LINE_COLOR (bswap_32(0x13082CFFu))
#define SEL_LINE_WIDTH 4

VideoStreamInfo::VideoStreamInfo()
{

    mHeightScaleUpButton.setToolTip("Height Scale Up");
    mHeightScaleDownButton.setToolTip("Height Scale Down");
    mHeightScaleResetButton.setToolTip("Reset Height Scale");
    mWidthScaleUpButton.setToolTip("Width Scale Up");
    mWidthScaleDownButton.setToolTip("Width Scale Down");
    mWidthScaleResetButton.setToolTip("Reset Width Scale");
    mHistMoveLeftButton.setToolTip("Scroll Left");
    mHistMoveRightButton.setToolTip("Scroll Right");

    mNextFrameButton.setToolTip("Next Frame");
    mPrevFrameButton.setToolTip("Prev Frame");
    mPlayButton.setToolTip("Play");
    mPauseButton.setToolTip("Pause");

    mImageDisplay.open();
    mImageDisplay.addChildFlag(ImGuiChildFlags_Borders);
    mFrameDisplay.setHasCloseButton(false);
    mFrameDisplay.setSize({640, 360}, ImGuiCond_FirstUseEver);
    mFrameDisplay.setContent([this]() { showFrameDisplay(); });

    mPlayProgressBar.setCallbacks(
        [this](float progress)
        {
            mCurSelectFrame[mCurSelectTrack] = (uint32_t)(progress * mTotalVideoFrameCount);
            mSelectChanged                   = true;
        },
        [this]() -> float { return (float)mCurSelectFrame[mCurSelectTrack] / mTotalVideoFrameCount; });
}

VideoStreamInfo::~VideoStreamInfo()
{
    freeTexture(&mFrameTexture);
}

bool VideoStreamInfo::show_hist(bool updateScroll)
{

    bool frameSelectChanged = false;
    if (!getMp4DataShare().dataAvailable)
        return frameSelectChanged;

    ImGui::BeginChild("HistRender", ImVec2(mHistogramSize.x, mHistogramSize.y), 0);

    auto bgColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    bgColor.x *= 0.9f;
    bgColor.y *= 0.9f;
    bgColor.z *= 0.9f;
    ImGui::GetWindowDrawList()->AddRectFilled(mHistogramPos, mHistogramPos + mHistogramSize,
                                              ImGui::ColorConvertFloat4ToU32(bgColor));

    float    scrollbar_size    = ImGui::GetStyle().ScrollbarSize;
    // calculate the histogram size
    ImVec2   histogramShowSize = mHistogramSize - ImVec2(0, scrollbar_size);
    // let the max size frame be the max height of the histogram
    float    histDrawHeightMax = histogramShowSize.y;
    float    histColWidth      = histDrawHeightMax / 8 * mHistogramWidthScale;
    uint32_t showCols          = (uint32_t)floor(histogramShowSize.x / histColWidth);
    histogramShowSize.x        = showCols * histColWidth;
    float colBorderWidth       = histColWidth / 10;
    float selectLineWidth      = MIN(MAX(1, colBorderWidth), SEL_LINE_WIDTH);
    ImS64 scrollMax            = MAX(0, mTotalVideoFrameCount - showCols + 1);

    if (updateScroll)
    {
        if (mCurSelectFrame[mCurSelectTrack] > mHistogramScrollPos + showCols * 2 / 3)
        {
            if (mCurSelectFrame[mCurSelectTrack] < showCols * 2 / 3)
                mHistogramScrollPos = 0;
            else
                mHistogramScrollPos = mCurSelectFrame[mCurSelectTrack] - showCols * 2 / 3;
        }
        else if (mCurSelectFrame[mCurSelectTrack] < mHistogramScrollPos + showCols / 3)
        {
            if (mCurSelectFrame[mCurSelectTrack] < showCols / 3)
                mHistogramScrollPos = 0;
            else
                mHistogramScrollPos = mCurSelectFrame[mCurSelectTrack] - showCols / 3;
        }
        printf("mCurSelectFrame %d mHistogramScrollPos = %lld\n", mCurSelectFrame[mCurSelectTrack], mHistogramScrollPos);
    }

    if (mHistogramScrollPos < 0)
        mHistogramScrollPos = 0;
    else if (mHistogramScrollPos > scrollMax)
        mHistogramScrollPos = scrollMax;

    ImGuiWindow *parent_window       = ImGui::GetCurrentWindow();
    ImS64        scroll_visible_size = (ImS64)(ImGui::GetContentRegionAvail().x);

    parent_window->ScrollbarSizes.y = scrollbar_size; // Hack to use GetWindowScrollbarRect()

    ImRect  scrollbar_rect = ImGui::GetWindowScrollbarRect(parent_window, (ImGuiAxis)ImGuiAxis_X);
    ImGuiID scrollbar_id   = ImGui::GetWindowScrollbarID(parent_window, (ImGuiAxis)ImGuiAxis_X);
    // time 1000 to make the scrollbar more smooth
    scrollMax *= 1000;
    mHistogramScrollPos *= 1000;
    ImGui::ScrollbarEx(scrollbar_rect, scrollbar_id, (ImGuiAxis)ImGuiAxis_X, &mHistogramScrollPos, scroll_visible_size, scrollMax,
                       ImDrawFlags_RoundCornersAll);
    mHistogramScrollPos /= 1000;
    scrollMax /= 1000;

    parent_window->ScrollbarSizes.y = 0.0f; // Restore modified value

    // Scrolling region use remaining space
    ImGui::BeginChild("HistRender##Real", ImVec2(0, -scrollbar_size));
    mHistogramStartIdx = (uint32_t)mHistogramScrollPos;
    mHistogramEndIdx   = (uint32_t)MIN(mTotalVideoFrameCount - 1, mHistogramStartIdx + showCols - 1);
    auto &samples      = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;

    for (uint32_t frameIdx = mHistogramStartIdx; frameIdx <= mHistogramEndIdx; frameIdx++)
    {
        int               realFrameIdx = getMp4DataShare().tracksFramePtsList[mCurSelectTrack][frameIdx];
        H26X_FRAME_TYPE_E frameType    = samples[realFrameIdx].frameType;
        uint64_t          frameSize    = samples[realFrameIdx].sampleSize;
        ImVec2            colSize;
        if (getAppConfigure().logarithmicAxis)
            colSize = ImVec2(histColWidth, logf((float)frameSize) * histDrawHeightMax
                                               / logf((float)getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack])
                                               * mHistogramHeightScale);
        else
            colSize = ImVec2(histColWidth, frameSize * histDrawHeightMax / getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack]
                                               * mHistogramHeightScale);
        ImVec2 colPos = ImVec2((frameIdx - mHistogramScrollPos) * histColWidth, histDrawHeightMax - colSize.y);
        colPos += mHistogramPos;
        ImVec2 colInnerSize = ImVec2(histColWidth - colBorderWidth * 2, colSize.y - colBorderWidth);
        ImVec2 colInnerPos  = colPos + ImVec2(colBorderWidth, colBorderWidth);

        ImU32 colColor = 0;
        switch (frameType)
        {
            case H26X_FRAME_I:
                colColor = I_FRAME_COLOR;
                break;
            case H26X_FRAME_P:
                colColor = P_FRAME_COLOR;
                break;
            default:
            case H26X_FRAME_B:
                colColor = B_FRAME_COLOR;
                break;
                break;
        }
        ImGui::GetWindowDrawList()->AddRectFilled(colPos, colPos + colSize, BORDER_COLOR);
        ImGui::GetWindowDrawList()->AddRectFilled(colInnerPos, colInnerPos + colInnerSize, colColor);
        if (mCurSelectFrame[mCurSelectTrack] == frameIdx)
        {
            ImVec2 selLinePos  = ImVec2(colPos.x + (histColWidth - selectLineWidth) / 2, mHistogramPos.y);
            ImVec2 selLineSize = ImVec2(selectLineWidth, histogramShowSize.y);
            ImGui::GetWindowDrawList()->AddRectFilled(selLinePos, selLinePos + selLineSize, SEL_LINE_COLOR);
        }

        if (ImGui::IsWindowHovered()
            && ImGui::IsMouseHoveringRect(ImVec2(colPos.x, mHistogramPos.y),
                                          ImVec2(colPos.x + colSize.x, mHistogramPos.y + histogramShowSize.y))
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            mCurSelectFrame[mCurSelectTrack] = frameIdx;
            frameSelectChanged               = true;
        }
    }

    uint64_t curTime = gettime_ms();
    if (mHistMoveLeftButton.isActive())
    {
        if (mHistogramScrollPos > 0)
        {
            if (curTime - mLastMoveLeftTime > mMoveInterval)
            {
                mLastMoveLeftTime = curTime;
                mHistogramScrollPos -= 1;
                if (mHistogramScrollPos < 0)
                    mHistogramScrollPos = 0;
            }
        }
    }
    else if (mHistMoveLeftButton.isClicked())
    {
        mHistogramScrollPos -= 1;
        if (mHistogramScrollPos < 0)
            mHistogramScrollPos = 0;
    }

    if (mHistMoveRightButton.isActive())
    {
        if (mHistogramScrollPos < scrollMax)
        {
            if (curTime - mLastMoveRightTime > mMoveInterval)
            {
                mLastMoveRightTime = curTime;
                mHistogramScrollPos += 1;
                if (mHistogramScrollPos > scrollMax)
                    mHistogramScrollPos = scrollMax;
            }
        }
    }
    else if (mHistMoveRightButton.isClicked())
    {
        mHistogramScrollPos += 1;
        if (mHistogramScrollPos > scrollMax)
            mHistogramScrollPos = scrollMax;
    }

    ImGui::EndChild();

    ImGui::EndChild();

    return frameSelectChanged;
}

bool VideoStreamInfo::show()
{
    if (mDockId == 0)
        mDockId = ImGui::GetID("StreamInfoDockSpace");
    ImGuiID dockUpId   = 0;
    ImGuiID dockDownId = 0;

    int dockFlags = ImGuiDockNodeFlags_AutoHideTabBar | ImGuiDockNodeFlags_NoDockingOverCentralNode;

    ImGui::DockSpace(mDockId, ImVec2(0, 0), dockFlags);

    splitDock(mDockId, ImGuiDir_Up, 0.5f, &dockUpId, &dockDownId);

    float textHeight = ImGui::GetTextLineHeight();

    float buttonSize = MAX(ImGui::GetStyle().ItemInnerSpacing.y * 2 + textHeight,
                           ImGui::GetStyle().ItemInnerSpacing.x * 2 + ImGui::CalcTextSize("+").x);
    mHeightScaleUpButton.setItemSize({buttonSize, buttonSize});
    mHeightScaleDownButton.setItemSize({buttonSize, buttonSize});
    mHeightScaleResetButton.setItemSize({buttonSize, buttonSize});

    mWidthScaleUpButton.setItemSize({buttonSize, buttonSize});
    mWidthScaleDownButton.setItemSize({buttonSize, buttonSize});
    mWidthScaleResetButton.setItemSize({buttonSize, buttonSize});

    mHistMoveLeftButton.setItemSize({buttonSize, buttonSize});
    mHistMoveRightButton.setItemSize({buttonSize, buttonSize});

    ImGui::SetNextWindowDockID(dockUpId);
    ImGui::Begin("Stream Hist", 0, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("Stream Hist Child", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollbar);

    mWinSize = ImGui::GetWindowSize();
    mWinPos  = ImGui::GetWindowPos();

#define ITEM_SPACING (5)
    mSideBarPos   = mWinPos + ImVec2{ITEM_SPACING, ITEM_SPACING};
    mSideBarWidth = mButtonSize.x
                  + ImGui::CalcTextSize(std::to_string(getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack]).c_str()).x
                  + ITEM_SPACING;

    mBottomBarHeight = mButtonSize.y + textHeight + ITEM_SPACING * 3;

    mHistogramPos = mSideBarPos + ImVec2{mSideBarWidth + ITEM_SPACING, MAX(mButtonSize.y, textHeight) / 2};

    mHistogramSize.x = mWinPos.x + mWinSize.x - mHistogramPos.x;
    mHistogramSize.y = mWinPos.y + mWinSize.y - mHistogramPos.y - mBottomBarHeight;
    mHistogramSize.y = MIN(mHistogramSize.x / 4, mHistogramSize.y);

    ImVec2 buttonPos = mSideBarPos;
    ImGui::SetCursorScreenPos(buttonPos);
    mHeightScaleUpButton.showDisabled(mHistogramHeightScale >= HIST_H_MAX_SCALE);
    if (mHeightScaleUpButton.isClicked())
    {
        if (mHistogramHeightScale < HIST_H_MAX_SCALE)
        {
            mHistogramHeightScale += 0.1f;
            if (mHistogramHeightScale > HIST_H_MAX_SCALE)
                mHistogramHeightScale = HIST_H_MAX_SCALE;
            mHistogramMaxSize = (uint64_t)(getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack] / mHistogramHeightScale);
        }
    }
    mButtonSize = ImGui::GetItemRectSize();

    buttonPos.y = mHistogramPos.y + mHistogramSize.y - ImGui::GetStyle().ScrollbarSize - mButtonSize.y / 2;
    ImGui::SetCursorScreenPos(buttonPos);
    mHeightScaleDownButton.showDisabled(mHistogramHeightScale <= HIST_H_MIN_SCALE);
    if (mHeightScaleDownButton.isClicked())
    {
        if (mHistogramHeightScale > HIST_H_MIN_SCALE)
        {
            mHistogramHeightScale -= 0.1f;
            if (mHistogramHeightScale < HIST_H_MIN_SCALE)
                mHistogramHeightScale = HIST_H_MIN_SCALE;
            mHistogramMaxSize = (uint64_t)(getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack] / mHistogramHeightScale);
        }
    }

    buttonPos = mHeightScaleDownButton.itemPos() + ImVec2(0, mHeightScaleDownButton.itemSize().y + ITEM_SPACING);
    ImGui::SetCursorScreenPos(buttonPos);
    mHeightScaleResetButton.showDisabled(1.f == mHistogramHeightScale);
    if (mHeightScaleResetButton.isClicked())
    {
        mHistogramHeightScale = 1;
        mHistogramMaxSize     = getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack];
    }

    ImVec2 textPos = mHeightScaleUpButton.itemPos()
                   + ImVec2(mHeightScaleUpButton.itemSize().x, (mHeightScaleUpButton.itemSize().y - textHeight) / 2);
    ImGui::SetCursorScreenPos(textPos);
    ImGui::Text("%5llu", mHistogramMaxSize);

    textPos.y = mHeightScaleDownButton.itemPos().y + (mHeightScaleUpButton.itemSize().y - textHeight) / 2;
    ImGui::SetCursorScreenPos(textPos);
    ImGui::Text("%5d", 0);

    int extraLines = (int)((mHistogramSize.y - textHeight - ITEM_SPACING) / (textHeight + ITEM_SPACING));

    if (extraLines > 0)
    {
        extraLines    = MIN(2, extraLines);
        float spacing = (mHistogramSize.y - ImGui::GetStyle().ScrollbarSize - textHeight * (extraLines + 1)) / (extraLines + 1);
        for (int i = 0; i < extraLines; i++)
        {
            textPos.y -= (textHeight + spacing);
            ImGui::SetCursorScreenPos(textPos);
            if (getAppConfigure().logarithmicAxis)
                ImGui::Text("%5llu", (uint64_t)pow(M_E, log(mHistogramMaxSize) * (i + 1) / (extraLines + 1)));
            else
                ImGui::Text("%5llu", mHistogramMaxSize * (i + 1) / (extraLines + 1));
        }
    }

    ImGui::SetCursorScreenPos(mHistogramPos);

    bool playNextFrame = false;
    bool selectFrame   = false;
    if (mIsPlaying)
    {
        uint64_t curTimeMs = gettime_ms();
        if (curTimeMs - mLastPlayTimeMs >= mMoveInterval)
        {
            mLastPlayTimeMs = curTimeMs;
            mCurSelectFrame[mCurSelectTrack]++;
            if (mCurSelectFrame[mCurSelectTrack] > getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->sampleCount - 1)
            {
                // loop
                mCurSelectFrame[mCurSelectTrack] = 0;
                mHistogramScrollPos              = 0;
                selectFrame                      = true;
            }
            else
            {
                playNextFrame = true;
            }
        }
    }

    if (mFrameDisplay.isFocused())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        {
            if (mCurSelectFrame[mCurSelectTrack] < mTotalVideoFrameCount - 1)
            {
                mCurSelectFrame[mCurSelectTrack]++;
                selectFrame = true;
            }
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        {
            if (mCurSelectFrame[mCurSelectTrack] > 0)
            {
                mCurSelectFrame[mCurSelectTrack]--;
                selectFrame = true;
            }
        }

        if (ImGui::IsKeyReleased(ImGuiKey_Space, false))
        {
            mIsPlaying = !mIsPlaying;
        }
    }

    if (mNextFrameButton.isClicked() || mNextFrameButton.isActiveFor(500))

    {
        if (mCurSelectFrame[mCurSelectTrack] < getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->sampleCount - 1)
        {
            mCurSelectFrame[mCurSelectTrack]++;
            selectFrame = true;
        }
    }

    if (mPrevFrameButton.isClicked() || mPrevFrameButton.isActiveFor(500))
    {
        if (mCurSelectFrame[mCurSelectTrack] > 0)
        {
            mCurSelectFrame[mCurSelectTrack]--;
            selectFrame = true;
        }
    }

    if (show_hist(playNextFrame || selectFrame || mSelectChanged))
    {
        mIsPlaying  = false;
        selectFrame = true;
    }

    ImGui::SetCursorScreenPos({mHistogramPos.x, mHistogramPos.y + mHistogramSize.y + ITEM_SPACING});
    ImGui::Text("%u", mHistogramStartIdx + 1); // make it start from 1

    ImVec2 text_size = ImGui::CalcTextSize(std::to_string(mHistogramEndIdx + 1).c_str());
    ImGui::SetCursorScreenPos(
        {mHistogramPos.x + mHistogramSize.x - text_size.x, mHistogramPos.y + mHistogramSize.y + ITEM_SPACING});
    ImGui::Text("%u", mHistogramEndIdx + 1); // make it start from 1

    buttonPos = ImVec2(mHistogramPos.x - mWidthScaleResetButton.itemSize().x - ITEM_SPACING,
                       mHistogramPos.y + mHistogramSize.y + textHeight + ITEM_SPACING * 2);
    ImGui::SetCursorScreenPos(buttonPos);
    mWidthScaleResetButton.show();
    if (mWidthScaleResetButton.isClicked())
    {
        mHistogramWidthScale = 1.f;
    }

    buttonPos = buttonPos + ImVec2(mWidthScaleResetButton.itemSize().x + ITEM_SPACING, 0);
    ImGui::SetCursorScreenPos(buttonPos);
    mWidthScaleDownButton.showDisabled(mHistogramWidthScale <= HIST_W_MIN_SCALE);
    if (mWidthScaleDownButton.isClicked())
    {
        if (mHistogramWidthScale > HIST_W_MIN_SCALE)
        {
            mHistogramWidthScale /= 2.f;
            if (mHistogramWidthScale < HIST_W_MIN_SCALE)
                mHistogramWidthScale = HIST_W_MIN_SCALE;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(mHistogramPos.x + mHistogramSize.x - mWidthScaleUpButton.itemSize().x,
                                     mHistogramPos.y + mHistogramSize.y + textHeight + ITEM_SPACING * 2));
    mWidthScaleUpButton.showDisabled(mHistogramWidthScale >= HIST_W_MAX_SCALE);
    if (mWidthScaleUpButton.isClicked())
    {
        if (mHistogramWidthScale < HIST_W_MAX_SCALE)
        {
            mHistogramWidthScale *= 2.f;
            if (mHistogramWidthScale > HIST_W_MAX_SCALE)
                mHistogramWidthScale = HIST_W_MAX_SCALE;
        }
    }

    ImGui::SetCursorScreenPos(mWidthScaleDownButton.itemPos() + ImVec2(mWidthScaleDownButton.itemSize().x + ITEM_SPACING, 0));
    mHistMoveLeftButton.showDisabled(mHistogramStartIdx <= 0);

    ImGui::SetCursorScreenPos(mWidthScaleUpButton.itemPos() - ImVec2(mHistMoveRightButton.itemSize().x + ITEM_SPACING, 0));
    mHistMoveRightButton.showDisabled(mHistogramEndIdx >= mTotalVideoFrameCount - 1);

    float space = mHistMoveRightButton.itemPos().x - mHistMoveLeftButton.itemPos().x;
    if (space < 0)
    {
        space = 0;
    }

    float centralButtonsSize = mNextFrameButton.itemSize().x + mPrevFrameButton.itemSize().x + ITEM_SPACING;
    ImGui::SetCursorScreenPos(
        ImVec2(mHistMoveLeftButton.itemPos().x + (space - centralButtonsSize) / 2, mHistMoveLeftButton.itemPos().y));
    mPrevFrameButton.showDisabled(mCurSelectFrame[mCurSelectTrack] <= 0);

    ImGui::SetCursorScreenPos(
        ImVec2(mPrevFrameButton.itemPos().x + mPrevFrameButton.itemSize().x + ITEM_SPACING, mPrevFrameButton.itemPos().y));

    mNextFrameButton.showDisabled(mCurSelectFrame[mCurSelectTrack]
                                  >= getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->sampleCount - 1);

    if (getMp4DataShare().videoTracksIdx.size() > 1)
    {
        if (ImGui::BeginPopupContextWindow("Select Track", ImGuiPopupFlags_MouseButtonRight))
        {
            int newSelectTrackIdx = -1;
            for (auto &trackIdx : getMp4DataShare().videoTracksIdx)
            {
                string trackName = "Track " + std::to_string(trackIdx);
                if (ImGui::MenuItem(trackName.c_str(), nullptr, mCurSelectTrack == trackIdx))
                {
                    newSelectTrackIdx = trackIdx;
                }
            }
            if (newSelectTrackIdx >= 0 && (unsigned int)newSelectTrackIdx != mCurSelectTrack)
            {
                mCurSelectTrack = newSelectTrackIdx;
                updateData();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::EndChild(); // Stream Hist Child

    ImGui::End();

    if (selectFrame || playNextFrame || mSelectChanged)
        updateFrameTexture();

    bool frameChanged = mSelectChanged || selectFrame || playNextFrame;
    mSelectChanged    = false;

    // set mSelectChanged Here
    mFrameDisplay.show();

    ImGui::SetNextWindowDockID(dockDownId);
    ImGui::Begin("Frame Info");
    showFrameInfo();
    ImGui::End();

    return frameChanged;
}

void VideoStreamInfo::resetData()
{
    mCurSelectTrack = 0;
    mCurSelectFrame.clear();
    if (!getMp4DataShare().videoTracksIdx.empty())
    {
        mCurSelectTrack = getMp4DataShare().videoTracksIdx[0];
        for (auto &trackIdx : getMp4DataShare().videoTracksIdx)
        {
            mCurSelectFrame[trackIdx] = 0;
        }
    }
    updateData();
}

void VideoStreamInfo::updateData()
{
    freeTexture(&mFrameTexture);
    mImageDisplay.clear();

    if (getMp4DataShare().videoTracksIdx.empty())
        return;

    auto &samples = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;

    mTotalVideoFrameCount = (uint32_t)samples.size();

    mHistogramMaxSize = getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack];
    updateFrameTexture();
}

void VideoStreamInfo::showFrameInfo()
{
    ImGui::Text("Play Index: %u", mCurSelectFrame[mCurSelectTrack]);
    ImGui::Text("Index: %u", mCurrentFrameInfo.frameIdx);
    ImGui::Text("Type: %s", mCurrentFrameInfo.frameType.c_str());
    if (getAppConfigure().needShowInHex)
    {
        ImGui::Text("Data Offset: %#llx", mCurrentFrameInfo.frameOffset);
        ImGui::Text("Size: %#llx", mCurrentFrameInfo.frameSize);
    }
    else
    {
        ImGui::Text("Data Offset: %lld", mCurrentFrameInfo.frameOffset);
        ImGui::Text("Size: %lld", mCurrentFrameInfo.frameSize);
    }
    ImGui::Text("Dts: %.2fs", mCurrentFrameInfo.dtsMs / 1000.f);
    ImGui::Text("Pts: %.2fs", mCurrentFrameInfo.ptsMs / 1000.f);
}

void VideoStreamInfo::updateFrameInfo(unsigned int trackIdx, uint32_t frameIdx, H26X_FRAME_TYPE_E frameType)
{
    if (trackIdx == mCurSelectTrack && frameIdx == mCurSelectFrame[trackIdx])
    {
        mCurrentFrameInfo.frameType = mp4GetFrameTypeStr(frameType);
    }
}

void VideoStreamInfo::showFrameDisplay()
{
    ImVec2 ContentSize = ImGui::GetContentRegionAvail();
    ImVec2 ImageRegion = ContentSize - ImVec2(0, mPlayControlPanelSize.y);

    mImageDisplay.setSize(ImageRegion);
    mImageDisplay.show();

    ImVec2 controlPanelStart = ImGui::GetCursorScreenPos();
    mPlayProgressBar.show();

    if (mIsPlaying)
    {
        mPauseButton.show();

        if (mPauseButton.isClicked())
            mIsPlaying = false;
    }
    else
    {
        mPlayButton.show();
        if (mPlayButton.isClicked())
        {
            mIsPlaying      = true;
            mLastPlayTimeMs = gettime_ms();
        }
    }

    mPlayControlPanelSize = ImGui::GetCursorScreenPos() - controlPanelStart;
}
