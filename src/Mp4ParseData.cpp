
#include "imgui_common_tools.h"
#include "ImGuiBaseTypes.h"
#include "logger.h"

#include "Mp4Parser.h"
#include "Mp4ParseData.h"
#include "AppConfigure.h"

using std::shared_ptr;
using std::string;
using std::vector;

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

int Mp4ParseData::decodeFrameAt(uint32_t trackIdx, uint32_t frameIdx, MyAVFrame &frame,
                                const std::vector<AVPixelFormat> &acceptFormats)
{

    auto trackDecoder = mVideoDecoders.find(trackIdx);
    if (trackDecoder == mVideoDecoders.end())
        return -1;

    auto &decoder = trackDecoder->second;

    int        ret = 0;
    MyAVPacket packet;

    auto &samples = tracksInfo[trackIdx].mediaInfo->samplesInfo;
    if (frameIdx >= samples.size())
        return -1;

    uint32_t seekFrameIdx = 0;
    for (seekFrameIdx = frameIdx; seekFrameIdx > 0; seekFrameIdx--)
    {
        if (samples[seekFrameIdx].isKeyFrame)
            break;
    }

    avcodec_flush_buffers(decoder.get());

    mTracksDecodeStat[trackIdx].lastExtractFrameIdx = seekFrameIdx - 1;

    while (1)
    {
        if (decodeOneFrame(trackIdx, frame) < 0)
        {
            return -1;
        }

        if (mTracksDecodeStat[trackIdx].lastExtractFrameIdx == frameIdx)
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
    ret         = decoder.send_packet(packet);
    if (ret < 0)
    {
        Z_ERR("send_packet fail: {}\n", ffmpeg_make_err_string(ret));
        return -1;
    }
    mTracksDecodeStat[trackIdx].lastExtractFrameIdx = frameIdx;

    printf("send packet pts %d\n", packet->pts);

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
        ret = decoder.receive_frame(frame);
        if (ret == 0)
        {
            printf("get frame pts %d\n", frame->pts);

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
            ret = decoder.send_packet(nullptr);
        }
    }

    for (auto &sample : samples)
    {
        if ((int64_t)sample.ptsMs == frame->pts)
        {
            trackDecodeInfo.lastDecodedFrameIdx = sample.sampleIdx;
        }
    }

    return 0;
}

int Mp4ParseData::transformFrameFormat(MyAVFrame &frame, const std::vector<AVPixelFormat> &acceptFormats)
{
    Z_INFO("frame format {}\n", frame->format);
    Z_INFO("frame pict_type {}\n", frame->pict_type);
    Z_INFO("frame pts {}\n", frame->pts);

    Z_INFO("Get Frame Format {}\n", frame->format);

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
        frame = trans_frame;
    }

    Z_INFO("frame format {}\n", frame->format);

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

    ret = mFmtTransition.scale_frame(transFrame, frame);
    if (ret < 0)
    {
        Z_ERR("sws_scale err {}\n", ffmpeg_make_err_string(ret));
        return -1;
    }

    frame = transFrame;

    return 0;
}

int Mp4ParseData::decodeNextFrame(uint32_t trackIdx, MyAVFrame &frame, const std::vector<AVPixelFormat> &acceptFormats)
{

    auto it = mTracksDecodeStat.find(trackIdx);
    if (it == mTracksDecodeStat.end())
    {
        return decodeFrameAt(trackIdx, 0, frame, acceptFormats);
    }
    auto trackDecoder = mVideoDecoders.find(trackIdx);
    if (trackDecoder == mVideoDecoders.end())
        return -1;

    auto &decoder = trackDecoder->second;
    auto &samples = tracksInfo[trackIdx].mediaInfo->samplesInfo;

    auto &trackDecodeInfo = it->second;
    if (trackDecodeInfo.lastDecodedFrameIdx >= (int64_t)samples.size() - 1)
    {
        Z_ERR("lastDecodedFrameIdx {} >= samples size {}\n", trackDecodeInfo.lastDecodedFrameIdx,
              tracksInfo[trackIdx].mediaInfo->samplesInfo.size());
        return -1;
    }

    uint32_t targetFrameIdx  = (uint32_t)(trackDecodeInfo.lastDecodedFrameIdx + 1);
    uint32_t extractFrameIdx = (uint32_t)(trackDecodeInfo.lastExtractFrameIdx + 1);

    MyAVPacket packet;
    int        ret = 0;

    while (1)
    {
        while (1)
        {
            frame.clear();
            ret = decoder.receive_frame(frame);
            if (ret < 0)
            {
                if (AVERROR_EOF == ret || AVERROR(EAGAIN) == ret)
                {
                    break;
                }
                else
                {
                    Z_ERR("err {}\n", ffmpeg_make_err_string(ret));
                    return -1;
                }
            }
            break;
        }
        if (ret == 0)
        {
            break;
        }

        packet.clear();
        Mp4VideoFrame videoSample;
        ret = mParser->getVideoSample(trackIdx, extractFrameIdx, videoSample);
        if (ret < 0)
        {
            Z_ERR("err {}\n", ret);
            return -1;
        }
        packet.setBuffer(videoSample.sampleData.get(), (int)videoSample.dataSize);
        packet->pts = videoSample.ptsMs;
        packet->dts = videoSample.dtsMs;
        ret         = decoder.send_packet(packet);
        if (ret < 0)
        {
            Z_ERR("send_packet fail: {}\n", ffmpeg_make_err_string(ret));
            return -1;
        }
        trackDecodeInfo.lastExtractFrameIdx = extractFrameIdx;

        extractFrameIdx++;
        if (extractFrameIdx == samples.size())
        {
            ret = decoder.send_packet(nullptr);
        }
    }

    if (ret == 0)
    {
    }

    return 0;
}

void Mp4ParseData::updateData()
{
    if (!newDataAvailable)
        return;

    tracksInfo.clear();
    mVideoDecoders.clear();
    tracksMaxSampleSize.clear();
    videoTracksIdx.clear();

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

        int ret = decoder.init_decoder(
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
        newDataAvailable = true;
        if (!dataAvailable)
            dataAvailable = true;
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
        newDataAvailable = false;
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
    mVideoDecoders.clear();
    tracksInfo.clear();
    mFmtTransition.clear();

    tracksMaxSampleSize.clear();
    videoTracksIdx.clear();

    mTotalVideoFrameCount = 0;
    mParsingFrameCount    = 0;
    newDataAvailable      = false;
    dataAvailable         = false;
}
