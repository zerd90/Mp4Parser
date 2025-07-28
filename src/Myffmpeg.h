
#ifndef _MY_FFMPEG_H_
#define _MY_FFMPEG_H_

#include <assert.h>
#include <string.h>
#include <mutex>
#include <string>

extern "C"
{
#ifdef _MSC_VER
    // remove warnings from ffmpeg headers
    #pragma warning(push)
    #pragma warning(disable : 4244)
#endif
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#ifdef _MSC_VER
    #pragma warning(pop)
#endif
}

#include "basic_tools.h"

#ifdef MYFFMPEG_DEBUG
    #define myffmpeg_dbg(fmt, ...) fprintf(stderr, "[%s:%d]: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
    #define myffmpeg_dbg(fmt, ...) \
        do                         \
        {                          \
        } while (0)
#endif

#define CHECK_HANDLE(h)               \
    if (!h)                           \
    {                                 \
        myffmpeg_dbg("not Init!!\n"); \
        return AVERROR(EINVAL);       \
    }

#define CHECK_PACKET(pkt)                \
    if (!pkt)                            \
    {                                    \
        myffmpeg_dbg("NULL packet!!\n"); \
        return AVERROR(EINVAL);          \
    }
#define CHECK_FRAME(frm)                \
    if (!frm)                           \
    {                                   \
        myffmpeg_dbg("NULL frame!!\n"); \
        return AVERROR(EINVAL);         \
    }

#define BASIC_METHODS(av_type, var) \
    bool isInit()                   \
    {                               \
        return nullptr != var;      \
    }                               \
    av_type *get()                  \
    {                               \
        return var;                 \
    }                               \
    bool operator!()                \
    {                               \
        return !var;                \
    }                               \
    av_type *operator->()           \
    {                               \
        return var;                 \
    }                               \
    bool operator==(av_type *var1)  \
    {                               \
        return this->var == var1;   \
    }                               \
    bool operator!=(av_type *var1)  \
    {                               \
        return this->var != var1;   \
    }

static inline const char *ffmpeg_make_err_string(int errCode)
{
    static char averr_buf[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(averr_buf, sizeof(averr_buf), errCode);
}

namespace Myffmpeg
{
    using MutexGuard = std::lock_guard<std::mutex>;

    static int         g_log_level = AV_LOG_WARNING;
    static inline void ffmpeg_log_cb(void *mod, int level, const char *fmt, va_list vl)
    {
        UNUSED(mod);
        if (level > g_log_level)
            return;

        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, vl);
        if (level <= AV_LOG_FATAL)
        {
            printf("FFMPEG(FATAL): %s", buf);
        }
        else if (level <= AV_LOG_ERROR)
        {
            printf("FFMPEG(ERROR): %s", buf);
        }
        else if (level <= AV_LOG_WARNING)
        {
            printf("FFMPEG(WARNING): %s", buf);
        }
        else if (level <= AV_LOG_INFO)
        {
            printf("FFMPEG(INFO): %s", buf);
        }
        else if (level <= AV_LOG_VERBOSE)
        {
            printf("FFMPEG(VERBOSE): %s", buf);
        }
        else if (level <= AV_LOG_DEBUG)
        {
            printf("FFMPEG(DEBUG): %s", buf);
        }
        else if (level <= AV_LOG_TRACE)
        {
            printf("FFMPEG(TRACE): %s", buf);
        }
    }

    static inline void init_ffmpeg_environment(int log_level = AV_LOG_ERROR)
    {
        g_log_level = log_level;
        av_log_set_callback(ffmpeg_log_cb);
    }

}; // namespace Myffmpeg

class IResourcePtr
{
public:
    IResourcePtr()
    {
        allocResource();
        refCount = 1;
    }
    void addRef()
    {
        Myffmpeg::MutexGuard locker(refLock);
        refCount++;
    }
    void release()
    {
        Myffmpeg::MutexGuard locker(refLock);
        refCount--;
        if (0 == refCount)
        {
            releaseResource();
        }
    }

private:
    virtual void allocResource() {}
    virtual void releaseResource() {}

private:
    std::mutex refLock;
    int        refCount = 0;
    // _T *resource;
};
#ifdef AV_PIX_FMT_D3D12
    #define AV_PIX_FMT_D3D12 227
#endif
static inline bool isHardwareFormat(AVPixelFormat format)
{
    return AV_PIX_FMT_D3D11 == format || AV_PIX_FMT_D3D11VA_VLD == format || AV_PIX_FMT_DXVA2_VLD == format
        || AV_PIX_FMT_VIDEOTOOLBOX == format || AV_PIX_FMT_MEDIACODEC == format || AV_PIX_FMT_CUDA == format
        || AV_PIX_FMT_VAAPI == format || AV_PIX_FMT_VDPAU == format || AV_PIX_FMT_QSV == format || AV_PIX_FMT_MMAL == format
        || AV_PIX_FMT_OPENCL == format || AV_PIX_FMT_VULKAN == format || AV_PIX_FMT_D3D12 == format;
}
static inline std::vector<AVHWDeviceType> getSupportHWDeviceType()
{
    std::vector<AVHWDeviceType> hwTypes;
    AVHWDeviceType              type = AV_HWDEVICE_TYPE_NONE;
    while (1)
    {
        type = av_hwdevice_iterate_types(type);
        if (AV_HWDEVICE_TYPE_NONE == type)
            break;
        AVBufferRef *hwDeviceCtx = nullptr;
        int          ret         = av_hwdevice_ctx_create(&hwDeviceCtx, type, nullptr, nullptr, 0);
        if (0 == ret)
        {
            av_buffer_unref(&hwDeviceCtx);
            hwTypes.push_back(type);
        }
    }

    return hwTypes;
}
class MyAVFrame
{
public:
    MyAVFrame()
    {
        avFrame = av_frame_alloc();
        assert(avFrame);
    }
    MyAVFrame(MyAVFrame &src_frame)
    {
        this->clear();
        this->avFrame     = src_frame.avFrame;
        src_frame.avFrame = av_frame_alloc();
        assert(src_frame.avFrame);
    }

    ~MyAVFrame()
    {
        clear();
        if (avFrame)
            av_frame_free(&avFrame);
        avFrame = nullptr;
    }

    BASIC_METHODS(AVFrame, avFrame)

    void operator=(MyAVFrame &src_frame)
    {
        this->clear();
        if (avFrame)
            av_frame_free(&avFrame);
        this->avFrame     = src_frame.avFrame;
        src_frame.avFrame = av_frame_alloc();
        assert(src_frame.avFrame);
    }

    int getBuffer(int width, int height, AVPixelFormat fmt)
    {
        clear();

        avFrame->width  = width;
        avFrame->height = height;
        avFrame->format = fmt;
        return av_frame_get_buffer(avFrame, 0);
    }

    // this method only unref buffer, not free the frame
    void clear()
    {
        if (avFrame)
            av_frame_unref(avFrame);
    }

    bool empty() { return nullptr == avFrame->data[0]; }

private:
    AVFrame *avFrame = nullptr;
};

class MyAVPacket
{
public:
    MyAVPacket()
    {
        avPacket = av_packet_alloc();
        assert(avPacket);
    }
    MyAVPacket(MyAVPacket &srcPacket)
    {
        this->avPacket     = srcPacket.avPacket;
        srcPacket.avPacket = av_packet_alloc();
        assert(srcPacket.avPacket);
    }
    ~MyAVPacket()
    {
        clear();
        if (avPacket)
            av_packet_free(&avPacket);
        avPacket = nullptr;
    }

    BASIC_METHODS(AVPacket, avPacket)

    void operator=(MyAVPacket &srcPacket)
    {
        this->clear();
        this->avPacket     = srcPacket.avPacket;
        srcPacket.avPacket = av_packet_alloc();
        assert(srcPacket.avPacket);
    }

    int setBuffer(uint8_t *data, int size)
    {
        clear();
        avPacket->data = data;
        avPacket->size = size;
        return 0;
    }

    int getBuffer(int size)
    {
        clear();
        return av_packet_from_data(avPacket, (uint8_t *)av_malloc(size), size);
    }

    // this method only unref buffer, not free the avPacket
    void clear()
    {
        if (avPacket)
            av_packet_unref(avPacket);
    }

    bool empty() { return nullptr == avPacket->data; }

private:
    AVPacket *avPacket = nullptr;
};

class MyAVFormatContext
{
#define CHECK_INPUT()                  \
    if (m_format_dir != AVFormatInput) \
    {                                  \
        myffmpeg_dbg("not Input!!\n"); \
        return AVERROR(EINVAL);        \
    }

#define CHECK_OUTPUT()                  \
    if (m_format_dir != AVFormatOutput) \
    {                                   \
        myffmpeg_dbg("not Output!!\n"); \
        return AVERROR(EINVAL);         \
    }

public:
    MyAVFormatContext() {}
    ~MyAVFormatContext() { clear(); }

    BASIC_METHODS(AVFormatContext, formatContext)

    void clear()
    {
        if (formatContext)
        {
            if (formatContext->pb && AVFormatOutput == m_format_dir)
            {
                closeOutputIO();
            }
            if (AVFormatInput == m_format_dir)
                avformat_close_input(&formatContext);
            else
                avformat_free_context(formatContext);
            formatContext = nullptr;
        }
    }

    int open_input(const char *file_path, const AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr)
    {
        int ret;
        clear();
        ret = avformat_open_input(&formatContext, file_path, fmt, options);
        if (ret < 0)
        {
            myffmpeg_dbg("open Input(%s) fail!!: %s\n", file_path, ffmpeg_make_err_string(ret));
            return ret;
        }
        myffmpeg_dbg("open Input Success\n");
        ret = avformat_find_stream_info(formatContext, nullptr);
        if (ret < 0)
        {
            myffmpeg_dbg("Find Stream fail!!: %s\n", ffmpeg_make_err_string(ret));
            clear();
            return ret;
        }
        m_format_dir = AVFormatInput;
        return 0;
    }

    int open_output(const char *file_path, const AVOutputFormat *fmt = nullptr, const char *format_name = nullptr)
    {
        int ret = 0;
        clear();
        outputFilePath = file_path;
        ret            = avformat_alloc_output_context2(&formatContext, fmt, format_name, file_path);
        if (ret < 0)
        {
            myffmpeg_dbg("Could not create output context: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        m_format_dir = AVFormatOutput;
        return 0;
    }

    int newStream(const AVCodecParameters *codecPar, AVRational timeBase)
    {
        CHECK_HANDLE(formatContext);
        CHECK_OUTPUT();

        const AVCodec *avcodec    = avcodec_find_decoder(codecPar->codec_id);
        AVStream      *out_stream = avformat_new_stream(formatContext, avcodec);
        int            ret        = 0;

        if (!out_stream)
        {
            myffmpeg_dbg("Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }

        AVCodecContext *codec_ctx = avcodec_alloc_context3(avcodec);
        if (!codec_ctx)
        {
            myffmpeg_dbg("Failed allocating AVCodecContext\n");
            return AVERROR_UNKNOWN;
        }
        ret = avcodec_parameters_to_context(codec_ctx, codecPar);
        if (ret < 0)
        {
            myffmpeg_dbg("Failed to copy context from input to output stream codec context\n");
            avcodec_free_context(&codec_ctx);
            return ret;
        }
        codec_ctx->codec_tag = 0;
        if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
            codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        ret = avcodec_parameters_from_context(out_stream->codecpar, codec_ctx);
        avcodec_free_context(&codec_ctx);
        out_stream->time_base = timeBase;
        return ret;
    }

    int initOutputIO(AVDictionary **options = nullptr)
    {
        CHECK_HANDLE(formatContext);
        CHECK_OUTPUT();

        int ret = avio_open(&formatContext->pb, outputFilePath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            myffmpeg_dbg("Could not open output file '%s'\n", outputFilePath.c_str());
            return ret;
        }
        ret = avformat_write_header(formatContext, options);
        if (ret < 0)
        {
            myffmpeg_dbg("Error occurred when opening output file\n");
            return ret;
        }

        return ret;
    }

    int writePacket(AVPacket *packet)
    {
        CHECK_HANDLE(formatContext);
        CHECK_OUTPUT();

        int ret = av_interleaved_write_frame(formatContext, packet);
        if (ret)
        {
            myffmpeg_dbg("write frame fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        return ret;
    }

    int writePacket(MyAVPacket &packet) { return writePacket(packet.get()); }

    int closeOutputIO()
    {
        CHECK_HANDLE(formatContext);
        CHECK_HANDLE(formatContext->pb);
        CHECK_OUTPUT();
        int ret = 0;

        ret = av_write_trailer(formatContext);
        if (ret)
        {
            myffmpeg_dbg("write trailer fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        ret = avio_closep(&formatContext->pb);
        if (ret)
        {
            myffmpeg_dbg("avio_closep fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        return 0;
    }

    // get next video stream after v_idx, passthrough -1 to get the first video stream
    // return the index
    int get_next_video_stream(int v_idx)
    {
        CHECK_HANDLE(formatContext);
        CHECK_INPUT();
        for (unsigned int idx = v_idx + 1; idx < formatContext->nb_streams; idx++)
        {
            if (formatContext->streams[idx]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                return idx;
        }

        return -1;
    }

    // same as get_next_video_stream but get audio stream
    int get_next_audio_stream(int a_idx)
    {
        CHECK_HANDLE(formatContext);
        CHECK_INPUT();
        for (unsigned int idx = a_idx + 1; idx < formatContext->nb_streams; idx++)
        {
            if (formatContext->streams[idx]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                return idx;
        }

        return -1;
    }

    int get_packet(AVPacket *pkt)
    {
        CHECK_HANDLE(formatContext);
        CHECK_INPUT();
        CHECK_PACKET(pkt);

        int ret = av_read_frame(formatContext, pkt);
        if (ret < 0)
        {
            if (AVERROR_EOF != ret)
                myffmpeg_dbg("av_read_frame fail!!: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }

        return 0;
    }

    int get_packet(MyAVPacket &pkt) { return get_packet(pkt.get()); }

    int get_next_packet_from_stream(AVPacket *pkt, int stream_idx)
    {
        CHECK_HANDLE(formatContext);
        CHECK_INPUT();
        CHECK_PACKET(pkt);

        if (stream_idx < 0 || (unsigned int)stream_idx >= formatContext->nb_streams)
        {
            myffmpeg_dbg("stream idx err %d\n", stream_idx);
            return AVERROR(EINVAL);
        }
        while (1)
        {
            int ret = av_read_frame(formatContext, pkt);
            if (ret < 0)
            {
                if (AVERROR_EOF != ret)
                    myffmpeg_dbg("av_read_frame fail!!: %s\n", ffmpeg_make_err_string(ret));
                return ret;
            }

            if (pkt->stream_index == stream_idx)
            {
                break;
            }
            av_packet_unref(pkt);
        }

        return 0;
    }

    int get_next_packet_from_stream(MyAVPacket &pkt, int stream_idx)
    {
        return get_next_packet_from_stream(pkt.get(), stream_idx);
    }

private:
    AVFormatContext *formatContext = nullptr;
    std::string      outputFilePath;
    enum
    {
        AVFormatUnknown = -1,
        AVFormatInput   = 0,
        AVFormatOutput  = 1,
    } m_format_dir = AVFormatUnknown;
};

class MyAVCodecContext
{
public:
    MyAVCodecContext() {}
    ~MyAVCodecContext() { clear(); }

    BASIC_METHODS(AVCodecContext, codecContext)

    int init_decoder(AVFormatContext *formatContext, int stream_idx,
                     std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        clear();
        m_codec_type = AVCodecDecoder;
        return init_from_format(formatContext, stream_idx, avcodec_find_decoder, nullptr, setExtraParameter);
    }
    int init_decoder(MyAVFormatContext &fmt, int stream_idx, std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        return init_decoder(fmt.get(), stream_idx, setExtraParameter);
    }

    int init_decoder(AVCodecID codec_id, std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        clear();
        m_codec_type = AVCodecDecoder;
        return init_by_codec_id(codec_id, avcodec_find_decoder, nullptr, setExtraParameter);
    }

    int init_decoder(const char *codecName, std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        clear();
        m_codec_type = AVCodecDecoder;
        return init_by_codec_name(codecName, avcodec_find_decoder_by_name, nullptr, setExtraParameter);
    }

    int init_encoder(AVFormatContext *formatContext, int stream_idx, AVRational timeBase,
                     std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        clear();
        m_codec_type = AVCodecEncoder;
        return init_from_format(formatContext, stream_idx, avcodec_find_encoder, &timeBase, setExtraParameter);
    }

    int init_encoder(MyAVFormatContext &fmt, int stream_idx, AVRational timeBase,
                     std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        return init_encoder(fmt.get(), stream_idx, timeBase, setExtraParameter);
    }

    int init_encoder(AVCodecID codec_id, AVRational timeBase, std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        clear();
        m_codec_type = AVCodecEncoder;
        return init_by_codec_id(codec_id, avcodec_find_encoder, &timeBase, setExtraParameter);
    }

    int init_encoder(const char *codecName, AVRational timeBase,
                     std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        clear();
        m_codec_type = AVCodecEncoder;
        return init_by_codec_name(codecName, avcodec_find_encoder_by_name, &timeBase, setExtraParameter);
    }

    const AVCodec *get_codec() { return codec; }

#define CHECK_ENCODER()                            \
    if (m_codec_type != AVCodecEncoder)            \
    {                                              \
        myffmpeg_dbg("This is not a Encoder!!\n"); \
        return AVERROR(EINVAL);                    \
    }
#define CHECK_DECODER()                            \
    if (m_codec_type != AVCodecDecoder)            \
    {                                              \
        myffmpeg_dbg("This is not a Decoder!!\n"); \
        return AVERROR(EINVAL);                    \
    }

#define CHECK_OPENED()                                                                             \
    if (!opened)                                                                                   \
    {                                                                                              \
        myffmpeg_dbg("%s not Opened!!\n", AVCodecEncoder == m_codec_type ? "Encoder" : "Decoder"); \
        return AVERROR(EINVAL);                                                                    \
    }

    int send_packet(AVPacket *pkt)
    {
        CHECK_HANDLE(codecContext);
        CHECK_DECODER();
        CHECK_OPENED();
        if (!pkt || !pkt->data || 0 == pkt->size)
        {
            avcodec_send_packet(codecContext, nullptr);
            return 0;
        }

        int ret = avcodec_send_packet(codecContext, pkt);
        if (ret < 0)
        {
            if (ret != AVERROR(EAGAIN))
                myffmpeg_dbg("avcodec_send_packet fail: %p %s\n", codecContext, ffmpeg_make_err_string(ret));
            return ret;
        }
        return 0;
    }

    int send_packet(MyAVPacket &pkt) { return send_packet(pkt.get()); }

    int receive_frame(AVFrame *frm)
    {
        CHECK_HANDLE(codecContext);
        CHECK_DECODER();
        CHECK_OPENED();
        if (!frm)
        {
            myffmpeg_dbg("NULL frame!!\n");
            return AVERROR(EINVAL);
        }
        int ret = avcodec_receive_frame(codecContext, frm);
        if (ret < 0)
        {
            if (ret != AVERROR(EAGAIN))
                myffmpeg_dbg("avcodec_receive_frame fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        return 0;
    }

    int receive_frame(MyAVFrame &frm) { return receive_frame(frm.get()); }

    int send_frame(AVFrame *frm)
    {
        CHECK_HANDLE(codecContext);
        CHECK_ENCODER();
        CHECK_OPENED();

        if (!frm || !frm->data[0])
        {
            avcodec_send_frame(codecContext, nullptr);
            return 0;
        }

        int ret = avcodec_send_frame(codecContext, frm);
        if (ret < 0)
        {
            if (ret != AVERROR(EAGAIN))
                myffmpeg_dbg("avcodec_send_frame fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        return 0;
    }

    int send_frame(MyAVFrame &frm) { return send_frame(frm.get()); }

    int receive_packet(AVPacket *pkt)
    {
        CHECK_HANDLE(codecContext);
        CHECK_ENCODER();
        CHECK_OPENED();
        CHECK_PACKET(pkt);

        int ret = avcodec_receive_packet(codecContext, pkt);
        if (ret < 0)
        {
            if (ret != AVERROR(EAGAIN))
                myffmpeg_dbg("avcodec_receive_packet fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        return 0;
    }

    int receive_packet(MyAVPacket &pkt) { return receive_packet(pkt.get()); }

    // this will free the context
    void clear()
    {
        if (codecContext)
        {
            avcodec_free_context(&codecContext);
        }
        m_codec_type = AVCodecUnknown;
        opened       = false;
    }

private:
    int init(const AVCodecParameters *codec_par = nullptr, AVRational *timeBase = nullptr,
             std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        int ret = 0;
        if (AVCodecEncoder == m_codec_type && !timeBase)
        {
            myffmpeg_dbg("TimeBase need to be set for encoder\n");
            return -1;
        }
        codecContext = avcodec_alloc_context3(codec);
        if (!codecContext)
        {
            myffmpeg_dbg("alloc codecContext fail\n");
            return -3;
        }

        if (codec_par)
        {
            ret = avcodec_parameters_to_context(codecContext, codec_par);
            if (ret < 0)
            {
                myffmpeg_dbg("avcodec_parameters_to_context fail: %s\n", ffmpeg_make_err_string(ret));
                return ret;
            }
        }

        if (AVCodecEncoder == m_codec_type)
        {
            codecContext->time_base = *timeBase;
            myffmpeg_dbg("Set timeBase %d %d\n", codecContext->time_base.num, codecContext->time_base.den);
        }

        if (nullptr != setExtraParameter)
            setExtraParameter(codecContext);

        ret = avcodec_open2(codecContext, codec, NULL);
        if (ret < 0)
        {
            myffmpeg_dbg("avcodec_open2 fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        opened = true;

        return 0;
    }

    int init_from_format(AVFormatContext *formatContext, int stream_idx, const AVCodec *(*find_codec_func)(AVCodecID id),
                         AVRational *timeBase = nullptr, std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        AVCodecParameters *codec_par = formatContext->streams[stream_idx]->codecpar;
        if (!codec_par)
        {
            myffmpeg_dbg("Get CodecParameters fail\n");
            return -1;
        }
        codec = find_codec_func(codec_par->codec_id);
        if (!codec)
        {
            myffmpeg_dbg("get Codec fail\n");
            return -2;
        }

        return init(codec_par, timeBase, setExtraParameter);
    }
    int init_by_codec(const AVCodec *newCodec, AVRational *timeBase = nullptr,
                      std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        if (!newCodec)
            return -1;
        this->codec = newCodec;
        return init(nullptr, timeBase, setExtraParameter);
    }

    int init_by_codec_id(AVCodecID codec_id, const AVCodec *(*find_codec_func)(AVCodecID id), AVRational *timeBase = nullptr,
                         std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        codec = find_codec_func(codec_id);
        if (!codec)
        {
            myffmpeg_dbg("get Codec fail\n");
            return -2;
        }

        return init(nullptr, timeBase, setExtraParameter);
    }

    int init_by_codec_name(const char *codecName, const AVCodec *(*find_codec_func)(const char *codecName),
                           AVRational *timeBase = nullptr, std::function<void(AVCodecContext *)> setExtraParameter = nullptr)
    {
        codec = find_codec_func(codecName);
        if (!codec)
        {
            myffmpeg_dbg("get Codec fail\n");
            return -2;
        }
        myffmpeg_dbg("%s(%s)\n", codec->name, codec->long_name);
        return init(nullptr, timeBase, setExtraParameter);
    }

private:
    AVCodecContext *codecContext = nullptr;
    const AVCodec  *codec        = nullptr;
    bool            opened       = false;

    enum
    {
        AVCodecUnknown = -1,
        AVCodecEncoder = 0,
        AVCodecDecoder = 1,
    } m_codec_type = AVCodecUnknown;
};

class MySwsContext
{
public:
    BASIC_METHODS(SwsContext, mSwsContext)
    ~MySwsContext() { clear(); }
    void clear()
    {
        if (mSwsContext)
            sws_freeContext(mSwsContext);
        mSwsContext = nullptr;
    }
    int init(int srcW, int srcH, enum AVPixelFormat srcFormat, int dstW, int dstH, enum AVPixelFormat dstFormat, int flags = 0)
    {
        if (isInit() && srcW == mSrcWidth && srcH == mSrcHeight && srcFormat == mSrcFormat && dstW == mDstWidth
            && dstH == mDstHeight && dstFormat == mDstFormat && flags == mScaleFlags)
            return 0;

        clear();

        mSrcWidth   = srcW;
        mSrcHeight  = srcH;
        mSrcFormat  = srcFormat;
        mDstWidth   = dstW;
        mDstHeight  = dstH;
        mDstFormat  = dstFormat;
        mScaleFlags = flags;

        mSwsContext = sws_getContext(srcW, srcH, srcFormat, dstW, dstH, dstFormat, flags, nullptr, nullptr, nullptr);
        if (!mSwsContext)
            return AVERROR(EINVAL);
        return 0;
    }

    int scale_frame(MyAVFrame &dst, MyAVFrame &src) { return scale_frame(dst.get(), src.get()); }

    int scale_frame(AVFrame *dst, AVFrame *src)
    {
        int ret = sws_scale_frame(mSwsContext, dst, src);
        if (ret < 0)
        {
            myffmpeg_dbg("sws_scale_frame fail: %s\n", ffmpeg_make_err_string(ret));
        }
        return ret;
    }

private:
    AVPixelFormat mSrcFormat = AV_PIX_FMT_NONE, mDstFormat = AV_PIX_FMT_NONE;
    int           mSrcWidth = 0, mSrcHeight = 0, mDstWidth = 0, mDstHeight = 0;
    int           mScaleFlags = 0;
    SwsContext   *mSwsContext = nullptr;
};

class MySwrContext
{
public:
    BASIC_METHODS(SwrContext, swrContext)

    ~MySwrContext() { clear(); }

    void clear()
    {
        if (swrContext)
            swr_free(&swrContext);
        swrContext = nullptr;
    }

    int init(const AVChannelLayout *out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
             const AVChannelLayout *in_ch_layout, enum AVSampleFormat in_sample_fmt, int in_sample_rate)
    {
        int ret = swr_alloc_set_opts2(&swrContext, out_ch_layout, out_sample_fmt, out_sample_rate, in_ch_layout, in_sample_fmt,
                                      in_sample_rate, 0, nullptr);
        if (ret < 0)
        {
            myffmpeg_dbg("alloc fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        ret = swr_init(swrContext);
        if (ret < 0)
        {
            myffmpeg_dbg("swr_init fail: %s\n", ffmpeg_make_err_string(ret));
            return ret;
        }
        return 0;
    }

    // inCount/outCount is in samples per channel
    int convert(uint8_t **out, int outCount, const uint8_t **in, int inCount)
    {
        CHECK_HANDLE(swrContext);
        return swr_convert(swrContext, out, outCount, in, inCount);
    }

    int convertFrame(MyAVFrame &dst, MyAVFrame &src) { return convertFrame(dst.get(), src.get()); }

    int convertFrame(AVFrame *dst, AVFrame *src)
    {
        CHECK_HANDLE(swrContext);
        return swr_convert_frame(swrContext, dst, src);
    }

private:
    SwrContext *swrContext = nullptr;
};

#endif