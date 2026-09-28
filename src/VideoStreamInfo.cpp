
#include "bits.h"
#define IMGUI_DEFINE_MATH_OPERATORS

#include "VideoStreamInfo.h"
#include "Mp4ParseData.h"
#include "AppConfigure.h"
#include "timer.h"
#include "ImGuiApplication.h"
#include "ImGuiApiTypes.h"
#include "ImGuiCommonTools.h"
#include "ImGuiCommonTools.h"

#include "logger.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

extern "C"
{
#include <libavutil/pixdesc.h>
}

using std::string;
using namespace ImGui;

static int availableFrameRate[] = {1, 5, 10, 15, 20, 24, 30, 60, 90, 120};

static bool isFrameRateAllowed(int rate, float displayHz)
{
    // Allow a little slack so 59.94 Hz displays still accept the 60 option.
    return (float)rate <= displayHz + 0.5f;
}

static int clampFrameRateToDisplay(int rate, float displayHz)
{
    int best = availableFrameRate[0];
    for (int candidate : availableFrameRate)
    {
        if (!isFrameRateAllowed(candidate, displayHz))
            break;
        best = candidate;
        if (candidate == rate)
            return rate;
    }
    return best;
}

static void fillFrameRateComboItems(std::map<ImGui::ComboTag, std::string> &items)
{
    items.clear();
    const float displayHz = ImGui::getDisplayRefreshRate();
    for (int rate : availableFrameRate)
    {
        if (!isFrameRateAllowed(rate, displayHz))
            continue;
        items[rate] = std::to_string(rate);
    }
    if (items.empty())
        items[60] = "60";
}

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
    barPos.y += ImGui::GetStyle().ItemSpacing.y;

    ImGui::SetCursorScreenPos(barPos);

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

    ImGui::SetCursorScreenPos(barPos + ImVec2(0, sBlockSize.y + ImGui::GetStyle().ItemSpacing.y));
}

void VideoStreamInfo::updateCurrFrameInfo()
{
    if (!getMp4DataShare().dataAvailable)
    {
        mCurrentFrameInfo.reset();
        return;
    }
    auto &samples = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;
    auto &ptsList = getMp4DataShare().tracksFramePtsList[mCurSelectTrack];
    if (ptsList.empty())
    {
        mCurrentFrameInfo.reset();
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

static std::vector<AVPixelFormat> supportFormats = {
    AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_NV12,     AV_PIX_FMT_NV21,    AV_PIX_FMT_GRAY8,
#if IMGUI_RENDER_API == IMGUI_RENDER_API_DX11
    AV_PIX_FMT_D3D11,
#endif
};

ImGui::ImGuiImageFormat transFormat(AVPixelFormat format)
{
    switch (format)
    {
        default:
            return ImGui::ImGuiImageFormat_None;
        case AV_PIX_FMT_RGBA:
            return ImGui::ImGuiImageFormat_RGBA;
        case AV_PIX_FMT_BGRA:
            return ImGui::ImGuiImageFormat_BGRA;
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_YUVJ444P:
            return ImGui::ImGuiImageFormat_YUV444P;
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUVJ422P:
            return ImGui::ImGuiImageFormat_YUV422P;
        case AV_PIX_FMT_YUV411P:
        case AV_PIX_FMT_YUVJ411P:
            return ImGui::ImGuiImageFormat_YUV411P;
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            return ImGui::ImGuiImageFormat_YUV420P;
        case AV_PIX_FMT_NV12:
            return ImGui::ImGuiImageFormat_NV12;
        case AV_PIX_FMT_NV21:
            return ImGui::ImGuiImageFormat_NV21;
        case AV_PIX_FMT_GRAY8:
            return ImGui::ImGuiImageFormat_Gray;
#if IMGUI_RENDER_API == IMGUI_RENDER_API_DX11
        case AV_PIX_FMT_D3D11:
            return ImGui::ImGuiImageFormat_Dx11;
#endif
    }
}
namespace
{
    constexpr uint32_t kDecodePrefetch = 8;

    static uint64_t playIntervalUsFromFps(int fps)
    {
        if (fps <= 0)
            fps = 20;
        return 1000000ull / (uint64_t)fps;
    }

    struct PresentedImage
    {
        uint32_t                    trackIdx   = 0;
        uint32_t                    playIdx    = 0;
        int                         width      = 0;
        int                         height     = 0;
        ImGui::ImGuiImageFormat     format     = ImGui::ImGuiImageFormat_None;
        ImGui::ImGuiImageColorRange colorRange = ImGui::ImGuiImageColorRange_16_235;
        int                         planeCount = 0;
        int                         stride[4]  = {};
        std::unique_ptr<uint8_t[]>  plane[4];
    };

    bool makePresentedImage(MyAVFrame &frame, uint32_t trackIdx, uint32_t playIdx, PresentedImage &out)
    {
        MyAVFrame software;
        AVFrame  *src = frame.get();
        if (isHardwareFormat((AVPixelFormat)frame->format))
        {
            if (av_hwframe_transfer_data(software.get(), frame.get(), 0) < 0)
                return false;
            frame.copyPropsTo(software);
            src = software.get();
        }

        PresentedImage image;
        image.trackIdx = trackIdx;
        image.playIdx  = playIdx;
        image.format   = transFormat((AVPixelFormat)src->format);
        if (image.format == ImGui::ImGuiImageFormat_None)
            return false;
        if (AV_PIX_FMT_YUVJ444P == src->format || AV_PIX_FMT_YUVJ422P == src->format || AV_PIX_FMT_YUVJ411P == src->format
            || AV_PIX_FMT_YUVJ420P == src->format || src->color_range == AVCOL_RANGE_JPEG)
            image.colorRange = ImGui::ImGuiImageColorRange_0_255;
        else
            image.colorRange = ImGui::ImGuiImageColorRange_16_235;

        image.width                    = src->width;
        image.height                   = src->height;
        image.planeCount               = (int)getPlaneCount(image.format);
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)src->format);
        if (image.planeCount <= 0 || desc == nullptr)
            return false;

        for (int i = 0; i < image.planeCount; i++)
        {
            if (src->data[i] == nullptr || src->linesize[i] <= 0)
                return false;
            uint32_t planeHeight = i > 0 ? (uint32_t)AV_CEIL_RSHIFT(src->height, desc->log2_chroma_h) : (uint32_t)src->height;
            uint32_t planeSize   = (uint32_t)src->linesize[i] * planeHeight;
            image.plane[i]       = std::make_unique<uint8_t[]>(planeSize);
            memcpy(image.plane[i].get(), src->data[i], planeSize);
            image.stride[i] = src->linesize[i];
        }
        out = std::move(image);
        return true;
    }
} // namespace

struct VideoStreamInfo::VideoDecodeWorker
{
    VideoDecodeWorker()
    {
        mThread = std::thread([this]() { loop(); });
    }

    ~VideoDecodeWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mMu);
            mStop   = true;
            mPaused = true;
            mEpoch.fetch_add(1, std::memory_order_acq_rel);
            mWanted.clear();
        }
        mCv.notify_one();
        if (mThread.joinable())
            mThread.join();
    }

    void cancel()
    {
        std::lock_guard<std::mutex> lock(mMu);
        mPaused = true;
        mEpoch.fetch_add(1, std::memory_order_acq_rel);
        mWanted.clear();
        mFailed.clear();
        mReady.clear();
        mCv.notify_one();
    }

    void setRequest(uint32_t track, const std::vector<uint32_t> &playIndices)
    {
        std::lock_guard<std::mutex> lock(mMu);
        mPaused = false;
        if (playIndices.empty())
        {
            // Keep already-decoded frames; only stop asking for new work.
            mWanted.clear();
            return;
        }

        uint32_t newTarget = playIndices.front();
        bool     related   = track == mTrack;
        if (related)
        {
            auto nearIndex = [](uint32_t a, uint32_t b)
            {
                uint32_t dist = a > b ? a - b : b - a;
                return dist <= 16;
            };
            related = nearIndex(mAnchor, newTarget);
            if (!related)
            {
                for (uint32_t idx : mWanted)
                {
                    if (nearIndex(idx, newTarget))
                    {
                        related = true;
                        break;
                    }
                }
            }
            if (!related)
            {
                for (const auto &image : mReady)
                {
                    if (image.trackIdx == track && nearIndex(image.playIdx, newTarget))
                    {
                        related = true;
                        break;
                    }
                }
            }
        }
        if (!related)
        {
            mEpoch.fetch_add(1, std::memory_order_acq_rel);
            mFailed.clear();
            mReady.clear();
        }
        else
        {
            mReady.erase(std::remove_if(mReady.begin(), mReady.end(), [&](const PresentedImage &image)
                                        { return image.trackIdx == track && image.playIdx < newTarget; }),
                         mReady.end());
        }
        mTrack  = track;
        mAnchor = newTarget;
        mWanted = playIndices;
        mCv.notify_one();
    }

    bool hasFrame(uint32_t track, uint32_t playIdx)
    {
        std::lock_guard<std::mutex> lock(mMu);
        return findReady(track, playIdx) != mReady.end();
    }

    bool isFailed(uint32_t track, uint32_t playIdx)
    {
        std::lock_guard<std::mutex> lock(mMu);
        if (track != mTrack)
            return false;
        return std::find(mFailed.begin(), mFailed.end(), playIdx) != mFailed.end();
    }

    bool takeFrame(uint32_t track, uint32_t playIdx, PresentedImage &out)
    {
        std::lock_guard<std::mutex> lock(mMu);
        auto                        it = findReady(track, playIdx);
        if (it == mReady.end())
            return false;
        out = std::move(*it);
        mReady.erase(it);
        return true;
    }

private:
    std::deque<PresentedImage>::iterator findReady(uint32_t track, uint32_t playIdx)
    {
        return std::find_if(mReady.begin(), mReady.end(),
                            [&](const PresentedImage &image) { return image.trackIdx == track && image.playIdx == playIdx; });
    }

    bool hasJob(uint32_t &track, uint32_t &playIdx, uint32_t &epoch)
    {
        if (mPaused || mWanted.empty())
            return false;
        for (uint32_t idx : mWanted)
        {
            if (findReady(mTrack, idx) != mReady.end())
                continue;
            if (std::find(mFailed.begin(), mFailed.end(), idx) != mFailed.end())
                continue;
            track   = mTrack;
            playIdx = idx;
            epoch   = mEpoch.load(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    void loop()
    {
        while (true)
        {
            uint32_t track   = 0;
            uint32_t playIdx = 0;
            uint32_t epoch   = 0;
            {
                std::unique_lock<std::mutex> lock(mMu);
                mCv.wait(lock, [&] { return mStop || hasJob(track, playIdx, epoch); });
                if (mStop)
                    return;
            }

            uint32_t sampleIdx = 0;
            if (!getMp4DataShare().sampleIndexForPlayIndex(track, playIdx, sampleIdx))
            {
                std::lock_guard<std::mutex> lock(mMu);
                if (epoch == mEpoch.load(std::memory_order_acquire))
                    mFailed.push_back(playIdx);
                continue;
            }

            MyAVFrame frame;
            int       ret = getMp4DataShare().decodeFrameAt(track, sampleIdx, frame, supportFormats, &mEpoch, epoch);
            if (ret != 0)
            {
                if (ret < 0)
                {
                    std::lock_guard<std::mutex> lock(mMu);
                    if (epoch == mEpoch.load(std::memory_order_acquire))
                        mFailed.push_back(playIdx);
                }
                continue;
            }

            PresentedImage image;
            if (!makePresentedImage(frame, track, playIdx, image))
            {
                std::lock_guard<std::mutex> lock(mMu);
                if (epoch == mEpoch.load(std::memory_order_acquire))
                    mFailed.push_back(playIdx);
                continue;
            }

            std::lock_guard<std::mutex> lock(mMu);
            if (mPaused || epoch != mEpoch.load(std::memory_order_acquire))
                continue;
            if (std::find(mWanted.begin(), mWanted.end(), playIdx) == mWanted.end())
                continue;
            mReady.push_back(std::move(image));
            while (mReady.size() > 12)
                mReady.pop_front();
        }
    }

    std::thread                mThread;
    std::mutex                 mMu;
    std::condition_variable    mCv;
    std::atomic<uint32_t>      mEpoch{1};
    bool                       mStop   = false;
    bool                       mPaused = true;
    uint32_t                   mTrack  = 0;
    uint32_t                   mAnchor = 0;
    std::vector<uint32_t>      mWanted;
    std::vector<uint32_t>      mFailed;
    std::deque<PresentedImage> mReady;
};

static uint32_t getNextIFrame(const std::vector<uint32_t> &iFrameList, uint32_t curFrame);

void VideoStreamInfo::updateFrameTexture()
{
    presentReadyFrame();
}

void VideoStreamInfo::cancelDecode()
{
    if (mDecodeWorker)
        mDecodeWorker->cancel();
}

bool VideoStreamInfo::presentReadyFrame()
{
    if (!mDecodeWorker)
        return false;

    PresentedImage image;
    if (!mDecodeWorker->takeFrame(mCurSelectTrack, mCurSelectFrame[mCurSelectTrack], image))
        return false;

    // Same play index was already on screen — ignore re-decoded duplicates from prefetch.
    if (mPresentedTrack == image.trackIdx && mPresentedPlayIdx == image.playIdx)
        return false;

    ImageData imageData;
    imageData.format     = image.format;
    imageData.colorRange = image.colorRange;
    imageData.width      = (unsigned int)image.width;
    imageData.height     = (unsigned int)image.height;
    for (int i = 0; i < image.planeCount; i++)
    {
        imageData.plane[i]  = image.plane[i].get();
        imageData.stride[i] = (unsigned int)image.stride[i];
    }
    updateImageTexture(imageData, mFrameTexture);
    mImageDisplay.setTexture(mFrameTexture);
    mFrameDisplay.open();
    mPresentedTrack   = image.trackIdx;
    mPresentedPlayIdx = image.playIdx;
    notePresentedFrame();
    if (!mIsPlaying)
        SET_APPLICATION_STATUS("Frame %u", image.playIdx + 1);
    return true;
}

void VideoStreamInfo::notePresentedFrame()
{
    uint64_t now = gettime_ms();
    mPresentedTimesMs.push_back(now);
    while (!mPresentedTimesMs.empty() && now - mPresentedTimesMs.front() > 1000)
        mPresentedTimesMs.pop_front();
}

float VideoStreamInfo::actualFrameRate()
{
    uint64_t now = gettime_ms();
    while (!mPresentedTimesMs.empty() && now - mPresentedTimesMs.front() > 1000)
        mPresentedTimesMs.pop_front();
    if (mPresentedTimesMs.size() < 2)
        return 0.f;
    uint64_t span = mPresentedTimesMs.back() - mPresentedTimesMs.front();
    if (span == 0)
        return 0.f;
    return (mPresentedTimesMs.size() - 1) * 1000.f / (float)span;
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

    mPrevFrameButton.setToolTip("Prev Frame");
    mPrevIFrameButton.setToolTip("Prev I Frame");
    mNextFrameButton.setToolTip("Next Frame");
    mNextIFrameButton.setToolTip("Next I Frame");
    mPlayButton.setToolTip("Play");
    mPauseButton.setToolTip("Pause");

    mImageDisplay.setScaleLimit(1.f, 50.f);
    mImageDisplay.open();
    mImageDisplay.removeChildFlag(ImGuiChildFlags_Borders);
    mFrameDisplay.setHasCloseButton(false);
    mFrameDisplay.setSize({640, 360}, ImGuiCond_FirstUseEver);
    mFrameDisplay.setContent([this]() { showFrameDisplay(); });
    mFrameDisplay.addChildFlag(ImGuiChildFlags_Borders);

    mFrameRateCombo.setLabelPosition(true);
    mFrameRateCombo.setGetComboItemsCallback(fillFrameRateComboItems);
    mFrameRateCombo.addComboFlag(ImGuiComboFlags_WidthFitPreview);
    {
        float displayHz                  = ImGui::getDisplayRefreshRate();
        getAppConfigure().playFrameRate  = clampFrameRateToDisplay(getAppConfigure().playFrameRate, displayHz);
        getAppConfigure().playIFrameRate = clampFrameRateToDisplay(getAppConfigure().playIFrameRate, displayHz);
        mPlayIntervalUs                  = playIntervalUsFromFps(getAppConfigure().playFrameRate);
        mFrameRateCombo.setSelected(getAppConfigure().playFrameRate);
    }

    mPlayProgressBar.setCallbacks(
        [this](float progress)
        {
            uint32_t frameIdx = (uint32_t)(progress * mTotalVideoFrameCount);
            if (seekToFrame(frameIdx, true) < 0)
                return;
            mSelectChanged = true;
        },
        [this]() -> float { return (float)mCurSelectFrame[mCurSelectTrack] / mTotalVideoFrameCount; });

    mDecodeWorker = std::make_unique<VideoDecodeWorker>();
}

int VideoStreamInfo::seekToFrame(uint32_t frameIdx, bool seekToIFrame)
{
    auto ptsList = getMp4DataShare().tracksFramePtsList.find(mCurSelectTrack);
    if (ptsList == getMp4DataShare().tracksFramePtsList.end() || frameIdx >= ptsList->second.size())
        return -1;

    if (seekToIFrame)
    {
        auto &samples = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;
        auto &pts     = ptsList->second;
        for (uint32_t i = frameIdx;; --i)
        {
            if (pts[i] < samples.size() && samples[pts[i]].isKeyFrame)
            {
                frameIdx = i;
                break;
            }
            if (i == 0)
                break;
        }
    }

    mCurSelectFrame[mCurSelectTrack] = frameIdx;
    mSelectChanged                   = true;
    return 0;
}

VideoStreamInfo::~VideoStreamInfo()
{
    mDecodeWorker.reset();
    freeTexture(mFrameTexture);
}

bool VideoStreamInfo::drawHistogram(bool updateScroll)
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
        Z_INFO("mCurSelectFrame {} mHistogramScrollPos = {}\n", mCurSelectFrame[mCurSelectTrack], mHistogramScrollPos);
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
                                          ImVec2(colPos.x + colSize.x, mHistogramPos.y + histogramShowSize.y)))
        {
            BeginTooltip();
            ImGui::Text("FrameIdx: %d", frameIdx + 1);
            EndTooltip();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (seekToFrame(frameIdx) == 0)
                    frameSelectChanged = true;
            }
        }
    }

    uint64_t curTime    = gettime_ms();
    bool     isHovered  = IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    bool     wheelXDown = IsKeyDown(ImGuiKey_MouseWheelX);
    bool     wheelYDown = IsKeyDown(ImGuiKey_MouseWheelY);

    if (mHistMoveLeftButton.isActiveFor(200))
    {
        uint64_t interval = mMoveInterval;
        if (mHistMoveLeftButton.isActiveFor(1000))
            interval /= 2;
        if (mHistogramScrollPos > 0)
        {
            if (curTime - mLastMoveLeftTime > interval)
            {
                mLastMoveLeftTime = curTime;
                mHistogramScrollPos -= 1;
            }
        }
    }
    else if (mHistMoveLeftButton.isClicked())
    {
        mHistogramScrollPos -= 1;
    }

    if (isHovered && wheelXDown && GetIO().MouseWheelH < 0)
        mHistogramScrollPos += (int)(GetIO().MouseWheelH * 5);
    if (mHistogramScrollPos < 0)
        mHistogramScrollPos = 0;

    if (mHistMoveRightButton.isActiveFor(200))
    {
        uint64_t interval = mMoveInterval;
        if (mHistMoveRightButton.isActiveFor(1000))
            interval /= 2;
        if (mHistogramScrollPos < scrollMax)
        {
            if (curTime - mLastMoveRightTime > interval)
            {
                mLastMoveRightTime = curTime;
                mHistogramScrollPos += 1;
            }
        }
    }
    else if (mHistMoveRightButton.isClicked())
    {
        mHistogramScrollPos += 1;
    }

    if (isHovered && wheelXDown && GetIO().MouseWheelH > 0)
        mHistogramScrollPos += (int)(GetIO().MouseWheelH * 5);
    if (mHistogramScrollPos > scrollMax)
        mHistogramScrollPos = scrollMax;

    if (mWidthScaleDownButton.isClicked() || (isHovered && wheelYDown && GetIO().MouseWheel < 0))
    {
        if (mHistogramWidthScale > HIST_W_MIN_SCALE)
        {
            mHistogramWidthScale /= 2.f;
            if (mHistogramWidthScale < HIST_W_MIN_SCALE)
                mHistogramWidthScale = HIST_W_MIN_SCALE;
        }
    }
    if (mWidthScaleUpButton.isClicked() || (isHovered && wheelYDown && GetIO().MouseWheel > 0))
    {
        if (mHistogramWidthScale < HIST_W_MAX_SCALE)
        {
            mHistogramWidthScale *= 2.f;
            if (mHistogramWidthScale > HIST_W_MAX_SCALE)
                mHistogramWidthScale = HIST_W_MAX_SCALE;
        }
    }

    ImGui::EndChild();

    ImGui::EndChild();

    return frameSelectChanged;
}

#define HISTOGRAM_HEIGHT      (180)
#define HISTOGRAM_WIDTH_RATIO (2 / 3.f)

bool VideoStreamInfo::showHistogramAndFrameInfo(bool updateScroll)
{
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

    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("Stream Hist", ImVec2(avail.x * HISTOGRAM_WIDTH_RATIO, HISTOGRAM_HEIGHT), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar);

    mWinSize = ImGui::GetWindowSize();
    mWinPos  = ImGui::GetWindowPos();

#define ITEM_SPACING (5)
    mSideBarPos   = mWinPos + ImVec2{ITEM_SPACING, ITEM_SPACING};
    mSideBarWidth = mButtonSize.x
                  + ImGui::CalcTextSize(std::to_string(getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack]).c_str()).x
                  + ITEM_SPACING;

    mBottomBarHeight = mButtonSize.y + textHeight + ITEM_SPACING * 3;

    mHistogramPos = mSideBarPos + ImVec2{mSideBarWidth + ITEM_SPACING, MAX(mButtonSize.y, textHeight) / 2};

    mHistogramSize.x = mWinPos.x + mWinSize.x - mHistogramPos.x - ITEM_SPACING;
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
    {
        float displayHz                  = ImGui::getDisplayRefreshRate();
        getAppConfigure().playFrameRate  = clampFrameRateToDisplay(getAppConfigure().playFrameRate, displayHz);
        getAppConfigure().playIFrameRate = clampFrameRateToDisplay(getAppConfigure().playIFrameRate, displayHz);
    }

    bool selectFrame = false;

    if (drawHistogram(updateScroll || selectFrame || mSelectChanged))
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
    mWidthScaleResetButton.showDisabled(1.f == mHistogramWidthScale);
    if (mWidthScaleResetButton.isClicked())
    {
        mHistogramWidthScale = 1.f;
    }

    buttonPos = buttonPos + ImVec2(mWidthScaleResetButton.itemSize().x + ITEM_SPACING, 0);
    ImGui::SetCursorScreenPos(buttonPos);
    mWidthScaleDownButton.showDisabled(mHistogramWidthScale <= HIST_W_MIN_SCALE);

    ImGui::SetCursorScreenPos(ImVec2(mHistogramPos.x + mHistogramSize.x - mWidthScaleUpButton.itemSize().x,
                                     mHistogramPos.y + mHistogramSize.y + textHeight + ITEM_SPACING * 2));
    mWidthScaleUpButton.showDisabled(mHistogramWidthScale >= HIST_W_MAX_SCALE);

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

    ImGui::EndChild(); // Stream Hist

    return selectFrame;
}

static uint32_t getNextIFrame(const std::vector<uint32_t> &iFrameList, uint32_t curFrame)
{
    if (iFrameList.empty())
        return 0;

    for (auto iFrameIdx : iFrameList)
        if (curFrame < iFrameIdx)
            return iFrameIdx;

    return iFrameList.back();
}

uint32_t getPrevIFrame(const std::vector<uint32_t> &iFrameList, uint32_t curFrame)
{
    if (iFrameList.empty())
        return 0;

    for (auto it = iFrameList.rbegin(); it != iFrameList.rend(); it++)
        if (curFrame > *it)
            return *it;

    return iFrameList.front();
}

void VideoStreamInfo::submitDecodeRequest()
{
    if (!mDecodeWorker || getMp4DataShare().videoTracksIdx.empty())
        return;

    auto ptsList = getMp4DataShare().tracksFramePtsList.find(mCurSelectTrack);
    if (ptsList == getMp4DataShare().tracksFramePtsList.end() || ptsList->second.empty())
        return;

    uint32_t count = (uint32_t)ptsList->second.size();
    uint32_t cur   = mCurSelectFrame[mCurSelectTrack];
    if (cur >= count)
        cur = count - 1;

    std::vector<uint32_t> want;
    bool                  needCurrent = mPresentedTrack != mCurSelectTrack || mPresentedPlayIdx != cur;
    if (needCurrent)
        want.push_back(cur);

    if (mIsPlaying)
    {
        uint32_t n = cur;
        for (uint32_t i = 0; i < kDecodePrefetch; i++)
        {
            uint32_t next = n;
            if (getAppConfigure().onlyPlayIFrame)
            {
                auto &iFrames = getMp4DataShare().tracksIFrameList[mCurSelectTrack];
                if (iFrames.empty() || n >= iFrames.back())
                    break;
                next = getNextIFrame(iFrames, n);
                if (next <= n)
                    break;
            }
            else if (n + 1 >= count)
            {
                break;
            }
            else
            {
                next = n + 1;
            }
            want.push_back(next);
            n = next;
        }
    }
    mDecodeWorker->setRequest(mCurSelectTrack, want);
}

bool VideoStreamInfo::show()
{
    if (getMp4DataShare().videoTracksIdx.empty() || getMp4DataShare().tracksFramePtsList[mCurSelectTrack].empty()
        || (getMp4DataShare().isRunning() && OPERATION_PARSE_FILE == getMp4DataShare().getCurrentOperation()))
        return false;

    ImVec2 contentRegion = ImGui::GetContentRegionAvail();
    ImVec2 startPos      = ImGui::GetCursorScreenPos();
    ImVec2 frameDisplaySize =
        getAppConfigure().showFrameInfo ? ImVec2(contentRegion.x, contentRegion.y - HISTOGRAM_HEIGHT) : contentRegion;

    bool playNextFrame = false;
    bool selectFrame   = false;
    bool frameShown    = mPresentedTrack == mCurSelectTrack && mPresentedPlayIdx == mCurSelectFrame[mCurSelectTrack];

    auto    &ptsList    = getMp4DataShare().tracksFramePtsList[mCurSelectTrack];
    auto    &iFrames    = getMp4DataShare().tracksIFrameList[mCurSelectTrack];
    uint32_t frameCount = (uint32_t)ptsList.size();

    if (mIsPlaying && mDecodeWorker)
    {
        int fps         = getAppConfigure().onlyPlayIFrame ? getAppConfigure().playIFrameRate : getAppConfigure().playFrameRate;
        mPlayIntervalUs = playIntervalUsFromFps(fps);

        uint64_t nowUs = gettime_us();
        if (nowUs >= mLastPlayTimeUs + mPlayIntervalUs)
        {
            uint32_t cur   = mCurSelectFrame[mCurSelectTrack];
            bool     atEnd = false;
            uint32_t next  = cur;
            if (getAppConfigure().onlyPlayIFrame)
            {
                if (iFrames.empty() || cur >= iFrames.back())
                    atEnd = true;
                else
                    next = getNextIFrame(iFrames, cur);
            }
            else if (cur + 1 >= frameCount)
            {
                atEnd = true;
            }
            else
            {
                next = cur + 1;
            }

            if (atEnd)
            {
                if (AppConfigures::RestartOnEnd == getAppConfigure().playStrategy)
                {
                    mCurSelectFrame[mCurSelectTrack] = getAppConfigure().onlyPlayIFrame && !iFrames.empty() ? iFrames.front() : 0;
                    mHistogramScrollPos              = 0;
                    mLastPlayTimeUs                  = nowUs;
                    selectFrame                      = true;
                }
                else
                {
                    mIsPlaying = false;
                }
            }
            else if (mDecodeWorker->hasFrame(mCurSelectTrack, next) || mDecodeWorker->isFailed(mCurSelectTrack, next))
            {
                mCurSelectFrame[mCurSelectTrack] = next;
                // Keep a fixed cadence from the previous deadline. Using "now" here makes the
                // effective interval snap to UI refresh boundaries and under-runs the target FPS.
                mLastPlayTimeUs += mPlayIntervalUs;
                if (mLastPlayTimeUs + mPlayIntervalUs < nowUs)
                    mLastPlayTimeUs = nowUs;
                playNextFrame = true;
                frameShown    = false;
            }
        }
    }

    if (mFrameDisplay.isFocused())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        {
            if (frameShown && mCurSelectFrame[mCurSelectTrack] < frameCount - 1)
            {
                mCurSelectFrame[mCurSelectTrack]++;
                selectFrame = true;
                frameShown  = false;
            }
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        {
            if (frameShown && mCurSelectFrame[mCurSelectTrack] > 0)
            {
                mCurSelectFrame[mCurSelectTrack]--;
                selectFrame = true;
                frameShown  = false;
            }
        }

        if (ImGui::IsKeyReleased(ImGuiKey_Space, false))
        {
            mIsPlaying = !mIsPlaying;
            if (mIsPlaying)
                mLastPlayTimeUs = gettime_us();
        }
    }

    if (mNextFrameButton.isClicked() || mNextFrameButton.isActiveFor(500))
    {
        if (frameShown && mCurSelectFrame[mCurSelectTrack] + 1 < frameCount)
        {
            seekToFrame(mCurSelectFrame[mCurSelectTrack] + 1);
            selectFrame = true;
            frameShown  = false;
        }
        mIsPlaying = false;
    }

    if (mPrevFrameButton.isClicked() || mPrevFrameButton.isActiveFor(500))
    {
        if (frameShown && mCurSelectFrame[mCurSelectTrack] > 0)
        {
            seekToFrame(mCurSelectFrame[mCurSelectTrack] - 1);
            selectFrame = true;
            frameShown  = false;
        }
        mIsPlaying = false;
    }

    if (mPrevIFrameButton.isClicked() || mPrevIFrameButton.isActiveFor(500))
    {
        if (frameShown && mCurSelectFrame[mCurSelectTrack] > 0 && !iFrames.empty())
        {
            seekToFrame(getPrevIFrame(iFrames, mCurSelectFrame[mCurSelectTrack]));
            selectFrame = true;
            frameShown  = false;
        }
        mIsPlaying = false;
    }

    if (mNextIFrameButton.isClicked() || mNextIFrameButton.isActiveFor(500))
    {
        if (frameShown && !iFrames.empty() && mCurSelectFrame[mCurSelectTrack] < iFrames.back())
        {
            seekToFrame(getNextIFrame(iFrames, mCurSelectFrame[mCurSelectTrack]));
            selectFrame = true;
            frameShown  = false;
        }
        mIsPlaying = false;
    }

    if (getAppConfigure().showFrameInfo)
    {
        ImVec2 histogramWinPos = startPos + ImVec2(0, frameDisplaySize.y);
        ImGui::SetCursorScreenPos(histogramWinPos);
        selectFrame = showHistogramAndFrameInfo(playNextFrame || selectFrame) || selectFrame;
        ImGui::SetCursorScreenPos(histogramWinPos + ImVec2(contentRegion.x * HISTOGRAM_WIDTH_RATIO, 0));
        ImGui::BeginChild("Frame Info", ImVec2(contentRegion.x * (1 - HISTOGRAM_WIDTH_RATIO), HISTOGRAM_HEIGHT),
                          ImGuiChildFlags_Borders);
        updateCurrFrameInfo();
        showFrameInfo();
        ImGui::EndChild();
    }
    else
    {
        updateCurrFrameInfo();
    }

    submitDecodeRequest();
    bool presented = presentReadyFrame();
    if ((selectFrame || mSelectChanged) && !presented)
        SET_APPLICATION_STATUS("Decoding Frame %u", mCurSelectFrame[mCurSelectTrack] + 1);

    bool frameChanged = mSelectChanged || selectFrame || playNextFrame || presented;
    mSelectChanged    = false;

    ImGui::SetCursorScreenPos(startPos);
    mFrameDisplay.setSize(frameDisplaySize);
    // set mSelectChanged inside
    mFrameDisplay.show();

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
    freeTexture(mFrameTexture);
    mImageDisplay.clear();
    mPresentedTimesMs.clear();
    mPresentedTrack   = (uint32_t)-1;
    mPresentedPlayIdx = (uint32_t)-1;
    if (mDecodeWorker)
        mDecodeWorker->cancel();

    if (getMp4DataShare().videoTracksIdx.empty())
        return;

    auto &samples = getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->samplesInfo;

    mTotalVideoFrameCount = (uint32_t)samples.size();

    mHistogramMaxSize = getMp4DataShare().tracksMaxSampleSize[mCurSelectTrack];

    mIsPlaying = false;

    submitDecodeRequest();
}

void VideoStreamInfo::showFrameInfo()
{
    ImGui::Text("Play Index: %u", mCurSelectFrame[mCurSelectTrack] + 1);
    ImGui::Text("Actual FPS: %.1f", actualFrameRate());
    ImGui::Text("Index: %u", mCurrentFrameInfo.frameIdx + 1);
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

    {
        auto displayInfo = mImageDisplay.getDisplayInfo();
        if (displayInfo.scale != mImageDisplayInfo.scale)
        {
            mLastImageDisplayInfoChangeMs = gettime_ms();
        }
        mImageDisplayInfo = displayInfo;
    }

    static const uint64_t sScaleInfoShowTimeMs      = 1000;
    static const uint64_t sScaleInfoStartFadeTimeMs = 500;

    uint64_t nowMs = gettime_ms();
    if (nowMs - mLastImageDisplayInfoChangeMs < sScaleInfoShowTimeMs)
    {
        auto   timeElapsed = nowMs - mLastImageDisplayInfoChangeMs;
        ImRect imageDisplayRect =
            ImRect(mImageDisplay.getPos().x, mImageDisplay.getPos().y, mImageDisplay.getPos().x + mImageDisplay.getSize().x,
                   mImageDisplay.getPos().y + mImageDisplay.getSize().y);
        char text[16];
        snprintf(text, sizeof(text), "%.1fx", mImageDisplayInfo.scale);
        auto   textSize = ImGui::CalcTextSize(text);
        ImVec2 showpos =
            imageDisplayRect.Min
            + ImVec2(imageDisplayRect.GetWidth() - textSize.x - GetStyle().WindowPadding.x, GetStyle().WindowPadding.y);
        ImColor color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        if (timeElapsed > sScaleInfoStartFadeTimeMs)
        {
            color.Value.w =
                1.f - (float)(timeElapsed - sScaleInfoStartFadeTimeMs) / (sScaleInfoShowTimeMs - sScaleInfoStartFadeTimeMs);
        }
        ImGui::GetForegroundDrawList()->AddText(showpos, color, text);
    }

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
            mLastPlayTimeUs = gettime_us();
        }
    }

    SameLine();
    mPrevIFrameButton.showDisabled(mCurSelectFrame[mCurSelectTrack] <= 0);
    SameLine();
    mPrevFrameButton.showDisabled(mCurSelectFrame[mCurSelectTrack] <= 0);
    SameLine();
    mNextFrameButton.showDisabled(mCurSelectFrame[mCurSelectTrack]
                                  >= getMp4DataShare().tracksInfo[mCurSelectTrack].mediaInfo->sampleCount - 1);
    SameLine();
    mNextIFrameButton.showDisabled(mCurSelectFrame[mCurSelectTrack]
                                   >= getMp4DataShare().tracksIFrameList[mCurSelectTrack].back());

    SameLine();
    {
        float displayHz  = ImGui::getDisplayRefreshRate();
        int  &activeRate = getAppConfigure().onlyPlayIFrame ? getAppConfigure().playIFrameRate : getAppConfigure().playFrameRate;
        int   clamped    = clampFrameRateToDisplay(activeRate, displayHz);
        if (clamped != activeRate)
        {
            activeRate      = clamped;
            mPlayIntervalUs = playIntervalUsFromFps(clamped);
        }
        if (mFrameRateCombo.getSelected() != activeRate)
            mFrameRateCombo.setSelected(activeRate);
        mFrameRateCombo.show();
        if (mFrameRateCombo.selectChanged())
        {
            activeRate      = clampFrameRateToDisplay(mFrameRateCombo.getSelected(), displayHz);
            mPlayIntervalUs = playIntervalUsFromFps(activeRate);
            mFrameRateCombo.setSelected(activeRate);
        }
    }

    SameLine();
    if (getAppConfigure().showFrameInfo)
    {
        if (Button("Hide Frame Info"))
        {
            getAppConfigure().showFrameInfo = false;
        }
    }
    else
    {
        if (Button("Show Frame Info"))
        {
            getAppConfigure().showFrameInfo = true;
        }
    }
    SameLine();
    if (Button("Save Frame"))
    {
        saveFrameToFile();
    }
    SameLine();
    if (Checkbox("Only Play I Frame", &getAppConfigure().onlyPlayIFrame))
    {
        if (getAppConfigure().onlyPlayIFrame)
            mPlayIntervalUs = playIntervalUsFromFps(getAppConfigure().playIFrameRate);
        else
            mPlayIntervalUs = playIntervalUsFromFps(getAppConfigure().playFrameRate);
    }

    mPlayControlPanelSize = ImGui::GetCursorScreenPos() - controlPanelStart;
}

int VideoStreamInfo::saveFrameToFile()
{
    auto frameIdx = mCurSelectFrame[mCurSelectTrack];
    auto ptsList  = getMp4DataShare().tracksFramePtsList.find(mCurSelectTrack);
    if (ptsList == getMp4DataShare().tracksFramePtsList.end() || frameIdx >= ptsList->second.size())
        return -1;

    auto realIdx = ptsList->second[frameIdx];

    return getMp4DataShare().saveFrameToFile(mCurSelectTrack, realIdx);
}

void VideoStreamInfo::setImageSampleType(ImGui::ImGuiImageSampleType sampleType)
{
    mImageDisplay.setSampleType(sampleType);
}
