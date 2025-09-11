
#include <algorithm>
#include <filesystem>

#include "imgui_common_tools.h"
#include "ImGuiBaseTypes.h"
#include "logger.h"

#include "Mp4Parser.h"
#include "Mp4ParseData.h"
#include "AppConfigure.h"

using std::shared_ptr;
using std::string;
using std::vector;
namespace fs = std::filesystem;

using namespace ImGui;

StdMutex                 gDatalock;
shared_ptr<Mp4ParseData> gDataShare = nullptr;

AppConfigures gAppConfig;

AppConfigures &getAppConfigure()
{
    return gAppConfig;
}

Mp4ParseData &getMp4DataShare()
{
    if (nullptr == gDataShare)
    {
        StdMutexGuard locker(gDatalock);
        if (!gDataShare)
            gDataShare = std::make_shared<Mp4ParseData>();
    }
    return *gDataShare;
}

int Mp4ParseData::startParse(PARSE_OPERATION_E op)
{
    mOperation = op;
    return start();
}

int Mp4ParseData::seekToFrame(uint32_t trackIdx, uint32_t frameIdx, uint32_t &keyFrameIdx)
{
    auto trackDecoder = mVideoDecoders.find(trackIdx);
    if (trackDecoder == mVideoDecoders.end())
        return -1;

    auto &decoder = trackDecoder->second;

    MyAVPacket packet;

    auto &samples = tracksInfo[trackIdx].mediaInfo->samplesInfo;
    if (frameIdx >= samples.size())
        return -1;

    for (auto &cache : mDecodeFrameCache)
    {
        if (cache.ptsMs == samples[frameIdx].ptsMs)
        {
            Z_INFO("Got Cache With Pts {}\n", cache.ptsMs);
            return 1;
        }
    }

    uint32_t seekFrameIdx = 0;
    for (seekFrameIdx = frameIdx; seekFrameIdx > 0; seekFrameIdx--)
    {
        if (samples[seekFrameIdx].isKeyFrame)
            break;
    }
    bool needSeek = false;

    if (mTracksDecodeStat[trackIdx].lastDecodedFrameIdx < 0
        || samples[mTracksDecodeStat[trackIdx].lastDecodedFrameIdx].ptsMs >= samples[frameIdx].ptsMs)
    {
        needSeek = true;
    }
    else
    {
        for (int64_t i = mTracksDecodeStat[trackIdx].lastDecodedFrameIdx + 1; i <= frameIdx; i++)
        {
            if (samples[i].isKeyFrame)
            {
                needSeek = true;
                break;
            }
        }
    }

    if (needSeek)
    {
        avcodec_flush_buffers(decoder.get());
        mDecodeFrameCache.clear();
        mTracksDecodeStat[trackIdx].lastExtractFrameIdx = seekFrameIdx - 1;
        keyFrameIdx                                     = seekFrameIdx;
        return 0;
    }
    return 2;
}

int Mp4ParseData::decodeFrameAt(uint32_t trackIdx, uint32_t frameIdx, MyAVFrame &frame,
                                const std::vector<AVPixelFormat> &acceptFormats)
{

    auto trackDecoder = mVideoDecoders.find(trackIdx);
    if (trackDecoder == mVideoDecoders.end())
        return -1;

    auto &decoder = trackDecoder->second;

    MyAVPacket packet;

    auto &samples = tracksInfo[trackIdx].mediaInfo->samplesInfo;
    if (frameIdx >= samples.size())
        return -1;

    for (auto &cache : mDecodeFrameCache)
    {
        if (cache.ptsMs == samples[frameIdx].ptsMs)
        {
            Z_INFO("Got Cache With Pts {}\n", cache.ptsMs);
            decodeJpegToFrame(cache.jpegData.get(), cache.jpegSize, frame);
            frame->pts = samples[frameIdx].ptsMs;
            transformFrameFormat(frame, acceptFormats);
            return 0;
        }
    }

    uint32_t seekFrameIdx = 0;
    for (seekFrameIdx = frameIdx; seekFrameIdx > 0; seekFrameIdx--)
    {
        if (samples[seekFrameIdx].isKeyFrame)
            break;
    }
    bool needSeek = false;

    if (mTracksDecodeStat[trackIdx].lastDecodedFrameIdx < 0
        || samples[mTracksDecodeStat[trackIdx].lastDecodedFrameIdx].ptsMs >= samples[frameIdx].ptsMs)
    {
        needSeek = true;
    }
    else
    {
        for (int64_t i = mTracksDecodeStat[trackIdx].lastDecodedFrameIdx + 1; i <= frameIdx; i++)
        {
            if (samples[i].isKeyFrame)
            {
                needSeek = true;
                break;
            }
        }
    }

    if (needSeek)
    {
        avcodec_flush_buffers(decoder.get());
        mDecodeFrameCache.clear();
        mTracksDecodeStat[trackIdx].lastExtractFrameIdx = seekFrameIdx - 1;
    }
    while (1)
    {
        if (decodeOneFrame(trackIdx, frame) < 0)
        {
            return -1;
        }

        addFrameToCache(frame);

        if (mTracksDecodeStat[trackIdx].lastDecodedFrameIdx >= frameIdx)
        {
            break;
        }
    }

    transformFrameFormat(frame, acceptFormats);

    return 0;
}

int Mp4ParseData::sendPacketToDecoder(uint32_t trackIdx, uint32_t frameIdx)
{
    auto trackDecoder = mVideoDecoders.find(trackIdx);
    if (trackDecoder == mVideoDecoders.end())
        return -1;

    auto &decoder = trackDecoder->second;
    auto &samples = tracksInfo[trackIdx].mediaInfo->samplesInfo;

    if (frameIdx >= samples.size())
    {
        Z_ERR("no more frames {} >= {}\n", frameIdx, samples.size());
        return -1;
    }

    MyAVPacket packet;

    Mp4VideoFrame videoSample;

    int ret = mParser->getVideoSample(trackIdx, frameIdx, videoSample);
    if (ret < 0)
    {
        Z_ERR("err {}\n", ret);
        return -1;
    }
    packet.setBuffer(videoSample.sampleData.get(), (int)videoSample.dataSize);
    packet->pts = videoSample.ptsMs;
    packet->dts = videoSample.dtsMs;
    ret         = decoder.sendPacket(packet);
    if (ret < 0)
    {
        Z_ERR("send_packet fail: {}\n", ffmpeg_make_err_string(ret));
        return -1;
    }
    mTracksDecodeStat[trackIdx].lastExtractFrameIdx = frameIdx;

    printf("send packet pts %" PRId64 "\n", packet->pts);

    return 0;
}

int Mp4ParseData::decodeOneFrame(uint32_t trackIdx, MyAVFrame &frame)
{
    auto trackDecoder = mVideoDecoders.find(trackIdx);
    if (trackDecoder == mVideoDecoders.end())
        return -1;

    auto &decoder = trackDecoder->second;
    auto &samples = tracksInfo[trackIdx].mediaInfo->samplesInfo;

    auto &trackDecodeInfo = mTracksDecodeStat[trackIdx];

    int ret = 0;

    uint32_t extractFrameIdx = (uint32_t)(trackDecodeInfo.lastExtractFrameIdx + 1);

    while (1)
    {
        frame.clear();
        ret = decoder.receiveFrame(frame);
        if (ret == 0)
        {
            printf("get frame pts %" PRId64 "\n", frame->pts);
            break;
        }
        else if (ret < 0)
        {
            if (ret != AVERROR(EAGAIN))
            {
                Z_ERR("err {}\n", ffmpeg_make_err_string(ret));
                return -1;
            }
        }

        if (sendPacketToDecoder(trackIdx, extractFrameIdx) < 0)
        {
            return -1;
        }

        extractFrameIdx++;
        if (extractFrameIdx == samples.size())
        {
            ret = decoder.sendPacket(nullptr);
        }
    }

    auto frm = std::find_if(samples.begin(), samples.end(),
                            [&frame](const Mp4SampleItem &sample) { return (int64_t)sample.ptsMs == frame->pts; });
    if (frm == samples.end())
    {
        return -1;
    }

    trackDecodeInfo.lastDecodedFrameIdx = frm->sampleIdx;
    Z_INFO("frame sampleIdx {}\n", trackDecodeInfo.lastDecodedFrameIdx);

    return 0;
}

int Mp4ParseData::transformFrameFormat(MyAVFrame &frame, const std::vector<AVPixelFormat> &acceptFormats)
{
    Z_INFO("frame format {}\n", frame->format);
    Z_INFO("frame pict_type {}\n", frame->pict_type);
    Z_INFO("frame pts {}\n", frame->pts);

    int ret = 0;

    if (std::find(acceptFormats.begin(), acceptFormats.end(), (AVPixelFormat)frame->format) != acceptFormats.end())
    {
        return 0;
    }

    if (isHardwareFormat((AVPixelFormat)frame->format))
    {
        MyAVFrame trans_frame;
        ret = av_hwframe_transfer_data(trans_frame.get(), frame.get(), 0);
        if (ret < 0)
        {
            Z_ERR("av_hwframe_transfer_data fail: {}\n", ffmpeg_make_err_string(ret));
            return -1;
        }
        Z_INFO("trans format {}\n", trans_frame->format);
        frame.copyPropsTo(trans_frame);
        frame = trans_frame;
    }

    if (acceptFormats.empty()
        || std::find(acceptFormats.begin(), acceptFormats.end(), (AVPixelFormat)frame->format) != acceptFormats.end())
    {
        return 0;
    }

    MyAVFrame transFrame;

    ret = transFrame.getBuffer(frame->width, frame->height, acceptFormats[0]);
    if (ret < 0)
    {
        Z_ERR("get buffer for {}x{} fail: {}\n", frame->width, frame->height, ffmpeg_make_err_string(ret));
        return -1;
    }
    ret = mFmtTransition.init(frame->width, frame->height, (AVPixelFormat)frame->format, transFrame->width, transFrame->height,
                              (AVPixelFormat)transFrame->format, SWS_FAST_BILINEAR);
    if (ret < 0)
    {
        Z_ERR("sws_getContext fail\n");
        return -1;
    }

    ret = mFmtTransition.scaleFrame(transFrame, frame);
    if (ret < 0)
    {
        Z_ERR("sws_scale err {}\n", ffmpeg_make_err_string(ret));
        return -1;
    }

    frame.copyPropsTo(transFrame);
    frame = transFrame;
    Z_INFO("trans format {}\n", frame->format);

    return 0;
}

void Mp4ParseData::clearData()
{
    tracksInfo.clear();
    mVideoDecoders.clear();
    mFmtTransition.clear();
    tracksMaxSampleSize.clear();
    videoTracksIdx.clear();
    mDecodeFrameCache.clear();
    tracksFramePtsList.clear();

    mTotalVideoFrameCount = 0;
    mParsingFrameCount    = 0;
}

void Mp4ParseData::updateData()
{
    if (!dataAvailable)
        return;

    clearData();

    curFilePath = localToUtf8(mParser->getFilePath());

    auto tracks = mParser->getTracksInfo();
    for (auto &track : tracks)
    {
        Mp4TrackInfo copyTrackInfo;
        copyTrackInfo           = *track;
        copyTrackInfo.mediaInfo = nullptr;
        switch (copyTrackInfo.trackType)
        {
            default:
                copyTrackInfo.mediaInfo  = std::make_shared<Mp4MediaInfo>();
                *copyTrackInfo.mediaInfo = *track->mediaInfo;
                break;
            case TRACK_TYPE_VIDEO:
                copyTrackInfo.mediaInfo = std::make_shared<Mp4VideoInfo>();
                *std::dynamic_pointer_cast<Mp4VideoInfo>(copyTrackInfo.mediaInfo) =
                    *std::dynamic_pointer_cast<Mp4VideoInfo>(track->mediaInfo);
                break;
            case TRACK_TYPE_AUDIO:
                copyTrackInfo.mediaInfo = std::make_shared<Mp4AudioInfo>();
                *std::dynamic_pointer_cast<Mp4AudioInfo>(copyTrackInfo.mediaInfo) =
                    *std::dynamic_pointer_cast<Mp4AudioInfo>(track->mediaInfo);
                break;
        }
        if (copyTrackInfo.trackType == TRACK_TYPE_VIDEO)
        {
            tracksFramePtsList[(int)tracksInfo.size()].reserve(copyTrackInfo.mediaInfo->samplesInfo.size());
            auto &ptsList = tracksFramePtsList[(int)tracksInfo.size()];
            auto &samples = copyTrackInfo.mediaInfo->samplesInfo;
            for (auto &sample : samples)
            {
                ptsList.push_back((uint32_t)sample.sampleIdx);
            }
            std::sort(ptsList.begin(), ptsList.end(),
                      [&samples](uint32_t a, uint32_t b) { return samples[a].ptsMs < samples[b].ptsMs; });
        }

        tracksInfo.push_back(copyTrackInfo);
    }

    auto virtual_top_box = mParser->getBoxes();

    for (uint32_t i = 0; i < tracksInfo.size(); i++)
    {
        if (TRACK_TYPE_VIDEO == tracksInfo[i].trackType)
        {
            videoTracksIdx.push_back(i);
        }
    }

    for (auto &track : tracksInfo)
    {
        auto    &samples       = track.mediaInfo->samplesInfo;
        uint64_t maxSampleSize = 0;
        for (auto &sample : samples)
        {
            if (sample.sampleSize > maxSampleSize)
            {
                maxSampleSize = sample.sampleSize;
            }
        }
        tracksMaxSampleSize.push_back(maxSampleSize);
    }

    if (videoTracksIdx.empty())
        return;

    recreateDecoder();
}

void Mp4ParseData::recreateDecoder()
{
    mVideoDecoders.clear();
    for (auto &trackIdx : videoTracksIdx)
    {
        AVCodecID codecID;
        auto      codecType = mp4GetCodecType(tracksInfo[trackIdx].mediaInfo->codecCode);
        if (codecType == MP4_CODEC_H264)
            codecID = AV_CODEC_ID_H264;
        else if (codecType == MP4_CODEC_HEVC)
            codecID = AV_CODEC_ID_HEVC;
        else if (codecType == MP4_CODEC_MPEG4)
            codecID = AV_CODEC_ID_MPEG4;
        else if (codecType == MP4_CODEC_MJPEG)
            codecID = AV_CODEC_ID_MJPEG;
        else if (codecType == MP4_CODEC_JPEG2000)
            codecID = AV_CODEC_ID_JPEG2000;
        else if (codecType == MP4_CODEC_MPEG1VIDEO)
            codecID = AV_CODEC_ID_MPEG1VIDEO;
        else if (codecType == MP4_CODEC_MPEG2VIDEO)
            codecID = AV_CODEC_ID_MPEG2VIDEO;
        else if (codecType == MP4_CODEC_VP9)
            codecID = AV_CODEC_ID_VP9;
        else
            return;

        auto &decoder = mVideoDecoders.insert(std::make_pair(trackIdx, MyAVCodecContext())).first->second;

        int ret = decoder.initDecoder(
            codecID,
            [](AVCodecContext *ctx)
            {
                int ret = 0;
                if (0 == getAppConfigure().hardwareDecode)
                {
                    ret = av_hwdevice_ctx_create(&ctx->hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
                    if (ret < 0)
                    {
                        Z_ERR("Create D3D11VA fail {}\n", ffmpeg_make_err_string(ret));
                    }
                    else
                    {
                        Z_INFO("Create D3D11VA success\n");
                    }
                }
                else if (getAppConfigure().hardwareDecode > 0)
                {
                    ret = av_hwdevice_ctx_create(&ctx->hw_device_ctx, (AVHWDeviceType)getAppConfigure().hardwareDecode, nullptr,
                                                 nullptr, 0);
                    if (ret < 0)
                    {
                        Z_ERR("Create {} fail {}\n", av_hwdevice_get_type_name((AVHWDeviceType)getAppConfigure().hardwareDecode),
                              ffmpeg_make_err_string(ret));
                    }
                    else
                    {
                        Z_INFO("Create {} success\n",
                               av_hwdevice_get_type_name((AVHWDeviceType)getAppConfigure().hardwareDecode));
                    }
                }
            });
        if (ret < 0)
        {
            ADD_LOG("init decoder type %d fail %s\n", codecType, ffmpeg_make_err_string(ret));
            return;
        }
    }

    mTracksDecodeStat.clear();
}

void Mp4ParseData::run()
{
    if (OPERATION_PARSE_FILE == mOperation)
    {
        string filePath = utf8ToLocal(toParseFilePath);
        mParser->parse(filePath);

        if (!mParser->isParseSuccess())
        {
            Z_ERR("parse fail\n");
            string err = mParser->getErrorMessage();
            gUserApp->setStatus(err);
            while (!err.empty())
            {
                ADD_LOG("%s\n", err.c_str());
                err = mParser->getErrorMessage();
            }
            return;
        }
        auto tracks = mParser->getTracksInfo();
        for (auto &track : tracks)
        {
            if (TRACK_TYPE_VIDEO != track->trackType)
                continue;

            if (mp4GetCodecType(track->mediaInfo->codecCode) != MP4_CODEC_H264
                && mp4GetCodecType(track->mediaInfo->codecCode) != MP4_CODEC_HEVC)
                continue;

            mTotalVideoFrameCount += track->mediaInfo->samplesInfo.size();
        }

        dataAvailable = true;
        updateData();
    }
    else if (OPERATION_PARSE_FRAME_TYPE == mOperation)
    {
        mParsingFrameCount = 0;
        for (auto &track : tracksInfo)
        {
            if (track.trackType != TRACK_TYPE_VIDEO)
                continue;

            if (mp4GetCodecType(track.mediaInfo->codecCode) != MP4_CODEC_H264
                && mp4GetCodecType(track.mediaInfo->codecCode) != MP4_CODEC_HEVC)
                continue;

            for (uint32_t parsingFrameIdx = 0; parsingFrameIdx < track.mediaInfo->samplesInfo.size(); parsingFrameIdx++)
            {
                if (!mIsContinue)
                    return;

                track.mediaInfo->samplesInfo[parsingFrameIdx].frameType =
                    mParser->parseVideoNaluType(track.trakIndex, parsingFrameIdx);
                track.mediaInfo->samplesInfo[parsingFrameIdx].naluTypes =
                    mParser->getTracksInfo()[track.trakIndex]->mediaInfo->samplesInfo[parsingFrameIdx].naluTypes;
                if (nullptr != onFrameParsed)
                {
                    onFrameParsed(track.trakIndex, parsingFrameIdx, track.mediaInfo->samplesInfo[parsingFrameIdx].frameType);
                }
                Z_DBG("get frame {} type {}\n", parsingFrameIdx, track.mediaInfo->samplesInfo[parsingFrameIdx].frameType);
                mParsingFrameCount++;
            }
        }
    }
}

void Mp4ParseData::init(const std::function<void(MP4_LOG_LEVEL_E level, const char *str)> &logCallback)
{
    setMp4ParseLogCallback([logCallback](MP4_LOG_LEVEL_E level, const char *msg) { logCallback(level, msg); });
}

void Mp4ParseData::starting()
{
    mIsContinue = true;
    if (OPERATION_PARSE_FILE == mOperation)
        dataAvailable = false;
}

float Mp4ParseData::getParseFileProgress()
{
    return (float)mParser->getParseProgress();
}

float Mp4ParseData::getParseFrameTypeProgress()
{
    return (float)mParsingFrameCount / (float)mTotalVideoFrameCount;
}
void Mp4ParseData::stopping()
{
    mIsContinue = false;
}

void Mp4ParseData::clear()
{
    if (isRunning())
        stop();

    mParser->clear();

    clearData();

    dataAvailable = false;
}

int createJpegCodecs(int width, int height, MyAVCodecContext &encoder)
{
    int ret = 0;

    ret = encoder.initEncoder(AV_CODEC_ID_MJPEG, AVRational{1, 25},
                              [&width, &height](AVCodecContext *codecCtx)
                              {
                                  codecCtx->width   = width;
                                  codecCtx->height  = height;
                                  codecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
                                  return 0;
                              });
    if (ret < 0)
    {
        ADD_APPLICATION_LOG("create jpeg encoder fail %s\n", ffmpeg_make_err_string(ret));
        return ret;
    }
    return 0;
}

std::unique_ptr<uint8_t[]> Mp4ParseData::encodeFrameToJpeg(MyAVFrame &frame, uint32_t &jpegSize)
{
    MyAVCodecContext encoder;
    MyAVPacket       packet;
    int              ret = 0;

    if (createJpegCodecs(frame->width, frame->height, encoder) < 0)
    {
        Z_ERR("create jpeg encoder fail {}\n", ffmpeg_make_err_string(ret));
        return nullptr;
    }

    if (transformFrameFormat(frame, {AV_PIX_FMT_YUVJ420P}) < 0)
    {
        Z_ERR("transform frame format fail {}\n", ffmpeg_make_err_string(ret));
        return nullptr;
    }

    ret = encoder.sendFrame(frame);
    if (ret < 0)
    {
        Z_ERR("encode frame to jpeg fail {}\n", ffmpeg_make_err_string(ret));
        return nullptr;
    }

    ret = encoder.receivePacket(packet);
    if (ret < 0)
    {
        Z_ERR("encode frame to jpeg fail {}\n", ffmpeg_make_err_string(ret));
        return nullptr;
    }
    jpegSize      = packet->size;
    auto jpegData = std::make_unique<uint8_t[]>(packet->size);
    memcpy(jpegData.get(), packet->data, packet->size);
    return jpegData;
}

int Mp4ParseData::decodeJpegToFrame(uint8_t *jpegData, uint32_t jpegSize, MyAVFrame &frame)
{
    int              ret = 0;
    MyAVCodecContext decoder;
    ret = decoder.initDecoder(AV_CODEC_ID_MJPEG);
    if (ret < 0)
    {
        Z_ERR("create jpeg decoder fail {}\n", ffmpeg_make_err_string(ret));
        return ret;
    }

    MyAVPacket packet;

    packet.setBuffer(jpegData, jpegSize);

    ret = decoder.sendPacket(packet);
    if (ret < 0)
    {
        Z_ERR("decode jpeg to frame fail {}\n", ffmpeg_make_err_string(ret));
        return ret;
    }

    ret = decoder.receiveFrame(frame);
    if (ret < 0)
    {
        Z_ERR("decode jpeg to frame fail {}\n", ffmpeg_make_err_string(ret));
        return ret;
    }

    return 0;
}

FrameCacheData::FrameCacheData() {}

FrameCacheData::~FrameCacheData() {}

void Mp4ParseData::addFrameToCache(MyAVFrame &frame)
{
    std::unique_ptr<uint8_t[]> jpegData;
    uint32_t                   jpegSize = 0;

    jpegData = encodeFrameToJpeg(frame, jpegSize);
    if (jpegData == nullptr)
        return;

    FrameCacheData cacheData;
    cacheData.jpegData = std::move(jpegData);
    cacheData.jpegSize = jpegSize;
    cacheData.width    = frame->width;
    cacheData.height   = frame->height;
    cacheData.ptsMs    = (uint32_t)frame->pts;

    Z_INFO("Add Frame Pts {} To Cache\n", frame->pts);
    mDecodeFrameCache.emplace_back(std::move(cacheData));
}

int Mp4ParseData::saveFrameToFile(uint32_t trackIdx, uint32_t frameIdx)
{
    auto &samples = tracksInfo[trackIdx].mediaInfo->samplesInfo;
    if (frameIdx >= samples.size())
        return -1;

    FrameCacheData *cacheData = nullptr;

    for (auto &cache : mDecodeFrameCache)
    {
        if (cache.ptsMs == samples[frameIdx].ptsMs)
        {
            Z_INFO("Got Cache With Pts {}\n", cache.ptsMs);
            cacheData = &cache;
            break;
        }
    }

    if (!cacheData)
    {
        Z_ERR("No Cache With Pts {}\n", samples[frameIdx].ptsMs);
        return -1;
    }

    string filePath =
        localToUtf8(mParser->asBox()->getBoxTypeStr()) + string("_frame_") + std::to_string(frameIdx) + string(".jpg");
    filePath = (fs::path(getAppConfigure().saveFramePath) / filePath).string();

    FILE *fp = fopen(utf8ToLocal(filePath).c_str(), "wb");
    if (!fp)
    {
        Z_ERR("Open File {} Fail\n", filePath);
        return -1;
    }
    fwrite(cacheData->jpegData.get(), 1, cacheData->jpegSize, fp);
    fclose(fp);

    SET_APPLICATION_STATUS("Save Frame To %s", filePath.c_str());

    return 0;
}
