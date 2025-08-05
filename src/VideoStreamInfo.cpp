
#include "bits.h"
#define IMGUI_DEFINE_MATH_OPERATORS

#include "Mp4Parser.h"
#include "AppConfigure.h"
#include <logger.h>

using std::string;

void VideoStreamInfo::updateFrameInfo(MyAVFrame &frame)
{
    IM_UNUSED(frame);
    if (!getMp4DataShare().dataAvailable)
    {
        memset(&mCurrentFrameInfo, 0, sizeof(mCurrentFrameInfo));
        return;
    }
    auto &samples                 = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;
    mCurrentFrameInfo.frameType   = mp4GetFrameTypeStr(samples[mCurSelectFrame[mCurSelectTrack]].frameType);
    mCurrentFrameInfo.frameOffset = samples[mCurSelectFrame[mCurSelectTrack]].sampleOffset;
    mCurrentFrameInfo.frameSize   = samples[mCurSelectFrame[mCurSelectTrack]].sampleSize;
}

void VideoStreamInfo::updateFrameTexture()
{
    MyAVFrame frame;

    if (getMp4DataShare().decodeFrameAt(mCurSelectTrack, mCurSelectFrame[mCurSelectTrack], frame, {AV_PIX_FMT_RGBA}) < 0)
        return;

    updateFrameInfo(frame);

    updateImageTexture(&mFrameTexture, frame->data[0], frame->width, frame->height, frame->linesize[0]);

    mImageDisplay.setTexture(mFrameTexture);
    mImageDisplay.open();
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
    mImageDisplay.setSize({640, 360}, ImGuiCond_FirstUseEver);
    mImageDisplay.setHasCloseButton(false);
    mImageDisplay.open();
}

VideoStreamInfo::~VideoStreamInfo()
{
    freeTexture(&mFrameTexture);
}

bool VideoStreamInfo::show_hist()
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

    float  scrollbar_size    = ImGui::GetStyle().ScrollbarSize;
    // calculate the histogram size
    ImVec2 histogramShowSize = mHistogramSize - ImVec2(0, scrollbar_size);
    // let the max size frame be the max height of the histogram
    float  histDrawHeightMax = histogramShowSize.y;
    float  histColWidth      = histDrawHeightMax / 8 * mHistogramWidthScale;
    int    showCols          = (int)floor(histogramShowSize.x / histColWidth);
    histogramShowSize.x      = showCols * histColWidth;
    float colBorderWidth     = histColWidth / 10;
    float selectLineWidth    = colBorderWidth;
    ImS64 scrollMax          = MAX(0, mTotalVideoFrameCount - showCols + 1);
    scrollMax *= 1000;

    ImGuiWindow *parent_window       = ImGui::GetCurrentWindow();
    ImS64        scroll_visible_size = (ImS64)(ImGui::GetContentRegionAvail().x);

    parent_window->ScrollbarSizes.y = scrollbar_size; // Hack to use GetWindowScrollbarRect()

    ImRect  scrollbar_rect = ImGui::GetWindowScrollbarRect(parent_window, (ImGuiAxis)ImGuiAxis_X);
    ImGuiID scrollbar_id   = ImGui::GetWindowScrollbarID(parent_window, (ImGuiAxis)ImGuiAxis_X);
    mHistogramScrollPos *= 1000;
    ImGui::ScrollbarEx(scrollbar_rect, scrollbar_id, (ImGuiAxis)ImGuiAxis_X, &mHistogramScrollPos, scroll_visible_size, scrollMax,
                       ImDrawFlags_RoundCornersAll);
    mHistogramScrollPos /= 1000;

    parent_window->ScrollbarSizes.y = 0.0f; // Restore modified value

    // Scrolling region use remaining space
    ImGui::BeginChild("HistRender##Real", ImVec2(0, -scrollbar_size));
    mHistogramStartIdx = (uint32_t)mHistogramScrollPos;
    mHistogramEndIdx   = (uint32_t)MIN(mTotalVideoFrameCount - 1, mHistogramStartIdx + showCols - 1);
    auto &samples      = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;

    for (uint32_t frameIdx = mHistogramStartIdx; frameIdx <= mHistogramEndIdx; frameIdx++)
    {
        H26X_FRAME_TYPE_E frameType = samples[frameIdx].frameType;
        uint64_t          frameSize = samples[frameIdx].sampleSize;
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

    if (mHistMoveLeftButton.isClicked())
    {
        mHistogramScrollPos -= 1;
        if (mHistogramScrollPos < 0)
            mHistogramScrollPos = 0;
    }

    if (mHistMoveRightButton.isClicked())
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
    do
    {
        if (mDockId != 0)
            break;

        mDockId         = ImGui::GetID("StreamInfoDockSpace");
        ImGuiID dock_id = mDockId;

        if (ImGui::DockBuilderGetNode(dock_id)) // not fist open
            break;

        ImGui::DockBuilderRemoveNode(dock_id);
        ImGui::DockBuilderAddNode(dock_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_id_top       = ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Up, 0.4f, nullptr, &dock_id);
        ImGuiID dock_id_down_left = ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Left, 0.75f, nullptr, &dock_id);

        ImGui::DockBuilderDockWindow("Stream Hist", dock_id_top);
        ImGui::DockBuilderDockWindow("Frame", dock_id_down_left);
        ImGui::DockBuilderDockWindow("Frame Info", dock_id);

        ImGui::DockBuilderFinish(mDockId);
    } while (0);
    ImGui::DockSpace(mDockId, ImVec2(0, 0), ImGuiDockNodeFlags_AutoHideTabBar);

    ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
    float  textHeight  = ImGui::GetTextLineHeight();

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
#define ITEM_SPACING (5)
    mSideBarPos   = mWinPos + ImVec2{ITEM_SPACING, ITEM_SPACING};
    mSideBarWidth = mButtonSize.x
                  + ImGui::CalcTextSize(std::to_string(getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack]).c_str()).x
                  + ITEM_SPACING;

    mBottomBarHeight = mButtonSize.y + textHeight + ITEM_SPACING * 3;

    mHistogramPos = mSideBarPos + ImVec2{mSideBarWidth + ITEM_SPACING, MAX(mButtonSize.y, textHeight) / 2};

    ImGui::Begin("Stream Hist", 0, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("Stream Hist Child", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollbar);

    mWinSize = ImGui::GetWindowSize();
    mWinPos  = ImGui::GetWindowPos();

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

    bool frameSelectChanged = show_hist();

    ImGui::SetCursorScreenPos({mHistogramPos.x, mHistogramPos.y + mHistogramSize.y + ITEM_SPACING});
    ImGui::Text("%u", mHistogramStartIdx + 1); // make it start from 1
    // Z_INFO("mHistogramSize.x {}, hist_show_w = {}, mHistogramStartIdx {}, mHistogramEndIdx {}\n", mHistogramSize.x,
    // hist_show_w,
    //    mHistogramStartIdx, mHistogramEndIdx);
    ImVec2 text_size = ImGui::CalcTextSize(std::to_string(mHistogramEndIdx).c_str());
    ImGui::SetCursorScreenPos(
        {mHistogramPos.x + mHistogramSize.x - text_size.x, mHistogramPos.y + mHistogramSize.y + ITEM_SPACING});
    ImGui::Text("%u", mHistogramEndIdx + 1); // make it start from 1

    buttonPos = mWidthScaleDownButton.itemPos() - ImVec2(mWidthScaleResetButton.itemSize().x + ITEM_SPACING, 0);
    ImGui::SetCursorScreenPos(buttonPos);
    mWidthScaleResetButton.show();
    if (mWidthScaleResetButton.isClicked())
    {
        mHistogramWidthScale = 1.f;
    }

    ImGui::SetCursorScreenPos(ImVec2(mHistogramPos.x, mHistogramPos.y + mHistogramSize.y + textHeight + ITEM_SPACING * 2));
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

    ImGui::EndChild();
    ImGui::End();

    if (frameSelectChanged)
        updateFrameTexture();

    mImageDisplay.show();

    if (getAppConfigure().needShowFrameInfo)
    {
        ImGui::Begin("Frame Info", &getAppConfigure().needShowFrameInfo);
        showFrameInfo();
        ImGui::End();
    }

    return frameSelectChanged;
}

void VideoStreamInfo::resetData()
{
    mCurSelectTrack = 0;
    mCurSelectFrame.clear();
    if (!getMp4DataShare().videoTracksIdx.empty())
    {
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
    mImageDisplay.close();

    if (getMp4DataShare().videoTracksIdx.empty())
        return;

    auto &samples = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;

    mTotalVideoFrameCount = (uint32_t)samples.size();

    mHistogramMaxSize = getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack];
    updateFrameTexture();
}

void VideoStreamInfo::showFrameInfo()
{
    ImGui::Text("Frame Index: %u", mCurSelectFrame[mCurSelectTrack] + 1); // make it start from 1
    ImGui::Text("Frame Type: %s", mCurrentFrameInfo.frameType.c_str());
    if (getAppConfigure().needShowInHex)
    {
        ImGui::Text("Frame Data Offset: %#llx", mCurrentFrameInfo.frameOffset);
        ImGui::Text("Frame Size: %#llx", mCurrentFrameInfo.frameSize);
    }
    else
    {
        ImGui::Text("Frame Data Offset: %lld", mCurrentFrameInfo.frameOffset);
        ImGui::Text("Frame Size: %lld", mCurrentFrameInfo.frameSize);
    }
}

void VideoStreamInfo::updateFrameInfo(unsigned int trackIdx, uint32_t frameIdx, H26X_FRAME_TYPE_E frameType)
{
    if (trackIdx == mCurSelectTrack && frameIdx == mCurSelectFrame[trackIdx])
    {
        mCurrentFrameInfo.frameType = mp4GetFrameTypeStr(frameType);
    }
}
