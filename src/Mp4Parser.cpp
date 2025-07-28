

#include <string>
#include <vector>
#include <memory>
#include <filesystem>

#include "logger.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "ImGuiApplication.h"
#include "ImGuiTools.h"
#include "imgui_common_tools.h"
#include "ImGuiItem.h"

#include "Mp4Parser.h"
#include "AppConfigure.h"

using std::ref;
using std::shared_ptr;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::vector;
namespace fs = std::filesystem;

// red
#define ERR_COLOR ImColor(255, 0, 0, 255)

#define IMPORTANT_LOG(fmt, ...)                                     \
    do                                                              \
    {                                                               \
        char impBuffer[1024];                                       \
        snprintf(impBuffer, sizeof(impBuffer), fmt, ##__VA_ARGS__); \
        ADD_LOG("%s", impBuffer);                                   \
        string status = impBuffer;                                  \
        while (status.back() == '\n' || status.back() == '\r')      \
            status.pop_back();                                      \
        gUserApp->setStatus(status);                                \
    } while (0)

#define IMPORTANT_ERR(fmt, ...)                                     \
    do                                                              \
    {                                                               \
        char impBuffer[1024];                                       \
        snprintf(impBuffer, sizeof(impBuffer), fmt, ##__VA_ARGS__); \
        string status = impBuffer;                                  \
        while (status.back() == '\n' || status.back() == '\r')      \
            status.pop_back();                                      \
        gUserApp->setStatus(status, ERR_COLOR);                     \
    } while (0)

Mp4ParserApp gMp4ParserApp;

std::string getProperFilePathForStatus(const string &fullPath)
{
    return localToUtf8(fs::path(utf8ToLocal(fullPath)).filename().string());
}

void TextCentral(string text)
{
    float win_width  = ImGui::GetWindowSize().x;
    float text_width = ImGui::CalcTextSize(text.c_str()).x;

    // calculate the indentation that centers the text on one line, relative
    // to window left, regardless of the `ImGuiStyleVar_WindowPadding` value
    float text_indentation = (win_width - text_width) * 0.5f;

    // if text is too long to be drawn on one line, `text_indentation` can
    // become too small or even negative, so we check a minimum indentation
    float min_indentation = 20.0f;
    if (text_indentation <= min_indentation)
    {
        text_indentation = min_indentation;
    }

    ImGui::SetCursorPosX(text_indentation);
    ImGui::TextWrapped("%s", text.c_str());
}

bool isItemClicked(IMGUI_MOUSE_BUTTON *button)
{
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        if (button)
            *button = ImGuiMouseButton_Left;
        return true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        if (button)
            *button = ImGuiMouseButton_Right;
        return true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
    {
        if (button)
            *button = ImGuiMouseButton_Middle;
        return true;
    }
    return false;
}
ImS64 getBoxSize(void *boxInfo)
{
    auto pBoxInfo = static_cast<BoxInfo *>(boxInfo);
    return pBoxInfo->boxSize;
}
uint8_t getBoxData(ImS64 offset, void *boxInfo)
{
    auto pBoxInfo = static_cast<BoxInfo *>(boxInfo);
    if (!pBoxInfo->buffer || offset < pBoxInfo->bufferedOffset || offset >= pBoxInfo->bufferedOffset + pBoxInfo->bufferSize)
    {
        if (!pBoxInfo->buffer)
        {
            pBoxInfo->bufferSize = MIN(pBoxInfo->boxSize, MAX_BUFFERED_SIZE);
            pBoxInfo->buffer     = std::make_unique<uint8_t[]>(pBoxInfo->bufferSize);
        }

        memset(pBoxInfo->buffer.get(), 0, pBoxInfo->bufferSize);

        pBoxInfo->bufferedOffset = MAX(0, offset - pBoxInfo->bufferSize / 2);
        ImS64 loadSize           = MIN(pBoxInfo->boxSize - pBoxInfo->bufferedOffset, pBoxInfo->bufferSize);

        FILE *fp = fopen(getMp4DataShare().getParser()->getFilePath().c_str(), "rb");
        if (fp)
        {
            ImS64 seekPos = pBoxInfo->boxPosition + pBoxInfo->bufferedOffset;
            fseek64(fp, seekPos, SEEK_SET);
            Z_INFO("Seek To {}, load {}\n", seekPos, loadSize);
            ImS64 rd = fread(pBoxInfo->buffer.get(), 1, loadSize, fp);
            if (rd != loadSize)
            {
                Z_ERR("Read file error {}, {}, {}\n", seekPos, loadSize, rd);
            }
            fclose(fp);
        }
    }
    return pBoxInfo->buffer[offset - pBoxInfo->bufferedOffset];
}

void SaveBoxData(const string &filePath, void *boxInfo)
{
    auto  pBoxInfo = static_cast<BoxInfo *>(boxInfo);
    FILE *fp       = fopen(filePath.c_str(), "wb");
    if (!fp)
    {
        ADD_LOG("Open %s error: %s\n", filePath.c_str(), getSystemError().c_str());
        return;
    }
    FILE *srcFp = fopen(getMp4DataShare().getParser()->getFilePath().c_str(), "rb");
    if (!srcFp)
    {
        ADD_LOG("Open %s error: %s\n", getMp4DataShare().curFilePath.c_str(), getSystemError().c_str());
        fclose(fp);
        return;
    }

    if (!pBoxInfo->buffer)
    {
        pBoxInfo->bufferSize = MIN(MAX_BUFFERED_SIZE, pBoxInfo->boxSize);
        pBoxInfo->buffer     = std::make_unique<uint8_t[]>(pBoxInfo->bufferSize);
    }

    fseek64(srcFp, pBoxInfo->boxPosition, SEEK_SET);

    ImS64 remainSize = pBoxInfo->boxSize;
    while (remainSize > 0)
    {
        ImS64 loadSize = MIN(remainSize, pBoxInfo->bufferSize);
        fread(pBoxInfo->buffer.get(), 1, loadSize, srcFp);
        fwrite(pBoxInfo->buffer.get(), 1, loadSize, fp);
        remainSize -= loadSize;
    }
    fclose(fp);
    fclose(srcFp);
    IMPORTANT_LOG("Save To %s Success\n", localToUtf8(filePath).c_str());
}

void Mp4ParserApp::ShowTreeNode(BoxInfo *cur_box)
{
    if (!cur_box)
    {
        Z_ERR("cur_box is nullptr\n");
        return;
    }
    int node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

    bool node_opened = false;

    if (cur_box_select == cur_box)
        node_flags |= ImGuiTreeNodeFlags_Selected;
    if (cur_box->sub_list.size() == 0)
    {
        node_flags |= (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        ImGui::TreeNodeEx((cur_box->box_type + std::to_string(cur_box->box_index)).c_str(), node_flags, "%s",
                          cur_box->box_type.c_str());
    }
    else
    {
        if (BoxInfo::FORCE_OPEN == cur_box->open_state)
        {
            ImGui::SetNextItemOpen(true);
            cur_box->open_state = BoxInfo::OPENED;
        }
        else if (BoxInfo::FORCE_CLOSE == cur_box->open_state)
        {
            ImGui::SetNextItemOpen(false);
            cur_box->open_state = BoxInfo::CLOSED;
        }
        node_opened = ImGui::TreeNodeEx((cur_box->box_type + std::to_string(cur_box->box_index)).c_str(), node_flags, "%s",
                                        cur_box->box_type.c_str());
    }
    if (isItemClicked())
    {
        if (cur_box_select != cur_box)
            m_focus_changed = true;
        cur_box_select = cur_box;
        mBoxBinaryViewer.setUserData(cur_box);
        Z_INFO("cur select {}| clicked {} open {}\n", cur_box_select->box_type, isItemClicked(), node_opened);
    }

    if (node_opened)
    {
        mSomeTreeNodeOpened = true;
        for (auto &sub_box : cur_box->sub_list)
        {
            ShowTreeNode(sub_box.get());
        }
        ImGui::TreePop();
    }
}

void Mp4ParserApp::ShowBoxesTreeView()
{
    mSomeTreeNodeOpened = false;
    if (ImGui::BeginTabItem("Boxes"))
    {
        if (getMp4DataShare().dataAvailable)
        {
            ShowTreeNode(mVirtFileBox.get());
        }

        if (m_focus_on != FOCUS_ON_BOXES)
        {
            Z_INFO("focus on boxes\n");
            m_focus_changed = true;
            m_focus_on      = FOCUS_ON_BOXES;
        }

        ImGui::EndTabItem();
    }
}

void Mp4ParserApp::ShowTracksTreeView()
{
    if (ImGui::BeginTabItem("Tracks"))
    {
        if (getMp4DataShare().dataAvailable)
        {
            int tree_node_id = 0;

            int node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
                           | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            for (size_t idx = 0; idx < getMp4DataShare().tracksInfo.size(); idx++)
            {
                if (cur_track_select == (int)idx)
                    node_flags |= ImGuiTreeNodeFlags_Selected;
                else
                    node_flags &= (~ImGuiTreeNodeFlags_Selected);

                ImGui::TreeNodeEx((void *)(intptr_t)tree_node_id, node_flags, "%s",
                                  mp4GetTrackTypeName(getMp4DataShare().tracksInfo[idx].trackType).c_str());
                tree_node_id++;
                if (isItemClicked())
                {
                    if (cur_track_select != (int)idx)
                    {
                        m_focus_changed  = true;
                        cur_track_select = (int)idx;
                    }
                }
            }
        }

        if (m_focus_on != FOCUS_ON_TRACKS)
        {
            m_focus_changed = true;
            m_focus_on      = FOCUS_ON_TRACKS;
            Z_INFO("focus on tracks\n");
        }

        ImGui::EndTabItem();
    }
}

void Mp4ParserApp::set_all_open_state(BoxInfo *pBox, bool isClose)
{
    pBox->open_state = isClose ? BoxInfo::FORCE_CLOSE : BoxInfo::FORCE_OPEN;
    if (pBox->sub_list.size() > 0)
    {
        for (auto &sub_box : pBox->sub_list)
        {
            set_all_open_state(sub_box.get(), isClose);
        }
    }
}

void Mp4ParserApp::ShowInfoItem(uint64_t boxIdx, const std::string &key, const Mp4BoxData &value)
{
    if (value.getDataType() == MP4_BOX_DATA_TYPE_KEY_VALUE_PAIRS)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNode(key.c_str()))
        {
            for (size_t item_idx = 0; item_idx < value.size(); item_idx++)
            {
                string subKey = value.kvGetKey(item_idx);
                ShowInfoItem(boxIdx, subKey, *value.kvGetValue(subKey));
            }
            ImGui::TreePop();
        }
    }
    else if (value.getDataType() == MP4_BOX_DATA_TYPE_ARRAY)
    {
        string show_str = key + ": {";
        for (size_t arr_item_idx = 0; arr_item_idx < value.size(); arr_item_idx++)
        {
            show_str += value[arr_item_idx]->toString();
            if (arr_item_idx < value.size() - 1)
                show_str += ", ";
        }
        show_str += "}";

        ImGui::Text("%s", show_str.c_str());
    }
    else if (value.getDataType() == MP4_BOX_DATA_TYPE_TABLE)
    {
        auto table = mBoxInfoTables.find(&value);
        if (table == mBoxInfoTables.end())
            return;

        TextCentral(key);

        table->second.show();
    }
    else if (value.getDataType() == MP4_BOX_DATA_TYPE_BINARY)
    {
        ImGui::Text("%s: ", key.c_str());
        auto binaryViewer = mAllBoxes[boxIdx]->binaryValueViewers.find(key);
        if (binaryViewer != mAllBoxes[boxIdx]->binaryValueViewers.end())
            binaryViewer->second->show();
    }
    else
    {
        if (getAppConfigure().needShowInHex && (key.find("Offset") != string::npos || key.find("Size") != string::npos))
            ImGui::Text("%s: %s", key.c_str(), value.toHexString().c_str());
        else
            ImGui::Text("%s: %s", key.c_str(), value.toString().c_str());
    }
}

int readDataFromFile(const string &filePath, uint8_t *buffer, size_t offset, size_t size)
{
    FILE *fp = fopen(filePath.c_str(), "rb");
    if (!fp)
    {
        IMPORTANT_ERR("Open %s error: %s\n", filePath.c_str(), getSystemError().c_str());
        return -1;
    }

    fseek64(fp, offset, SEEK_SET);
    size_t readSize = fread(buffer, 1, size, fp);
    fclose(fp);

    if (readSize != size)
    {
        ADD_LOG("Read %s error: %s\n", filePath.c_str(), getSystemError().c_str());
    }

    return 0;
}
int Mp4ParserApp::updateData(int type, size_t trackIdx, size_t itemIdx)
{
    switch (type)
    {
        case 0: // sample
        {
            if (trackIdx >= getMp4DataShare().tracksInfo.size())
                return -1;

            auto &trackInfo = getMp4DataShare().tracksInfo[trackIdx];
            auto &samples   = trackInfo.mediaInfo->samplesInfo;
            if (itemIdx >= samples.size())
                return -1;

            auto parser = getMp4DataShare().getParser();

            unique_ptr<Mp4RawSample> pSample;

            int ret;

            if (TRACK_TYPE_VIDEO == trackInfo.trackType && getAppConfigure().showWrappedData)
            {
                auto pVideoFrame = std::make_unique<Mp4VideoFrame>();
                ret              = parser->getVideoSample((uint32_t)trackIdx, (uint32_t)itemIdx, *pVideoFrame);
                pSample          = std::move(pVideoFrame);
            }
            else if (TRACK_TYPE_AUDIO == trackInfo.trackType && getAppConfigure().showWrappedData)
            {
                auto pAudioFrame = std::make_unique<Mp4AudioFrame>();
                ret              = parser->getAudioSample((uint32_t)trackIdx, (uint32_t)itemIdx, *pAudioFrame);
                pSample          = std::move(pAudioFrame);
            }
            else
            {
                pSample = std::make_unique<Mp4RawSample>();
                ret     = parser->getSample((uint32_t)trackIdx, (uint32_t)itemIdx, *pSample);
            }

            if (ret < 0)
            {
                IMPORTANT_ERR("Get Track %zu Sample %zu error: %s\n", trackIdx, itemIdx, parser->getErrorMessage().c_str());
                return -1;
            }

            if (mBinaryData.bufferSize < pSample->dataSize)
            {
                mBinaryData.bufferSize = pSample->dataSize;
                mBinaryData.buffer     = std::make_unique<uint8_t[]>(mBinaryData.bufferSize);
            }

            memcpy(mBinaryData.buffer.get(), pSample->sampleData.get(), pSample->dataSize);
            mBinaryData.type     = 0;
            mBinaryData.trackIdx = trackIdx;
            mBinaryData.itemIdx  = itemIdx;
            break;
        }
        case 1: // chunk
        {
            if (trackIdx >= getMp4DataShare().tracksInfo.size())
                return -1;
            auto &trackInfo = getMp4DataShare().tracksInfo[trackIdx];

            auto &chunks = trackInfo.mediaInfo->chunksInfo;
            if (itemIdx >= chunks.size())
                return -1;

            auto   parser    = getMp4DataShare().getParser();
            size_t totalSize = 0;

            vector<std::unique_ptr<Mp4RawSample>> samples;
            for (size_t sampleIdx = chunks[itemIdx].sampleStartIdx;
                 sampleIdx < chunks[itemIdx].sampleStartIdx + chunks[itemIdx].sampleCount; sampleIdx++)
            {
                if (sampleIdx >= getMp4DataShare().tracksInfo[trackIdx].mediaInfo->samplesInfo.size())
                    break;
                unique_ptr<Mp4RawSample> sample;

                int ret = 0;

                if (TRACK_TYPE_VIDEO == trackInfo.trackType && getAppConfigure().showWrappedData)
                {
                    auto pVideoFrame = std::make_unique<Mp4VideoFrame>();
                    ret              = parser->getVideoSample((uint32_t)trackIdx, (uint32_t)sampleIdx, *pVideoFrame);
                    sample           = std::move(pVideoFrame);
                }
                else if (TRACK_TYPE_AUDIO == trackInfo.trackType && getAppConfigure().showWrappedData)
                {
                    auto pAudioFrame = std::make_unique<Mp4AudioFrame>();
                    ret              = parser->getAudioSample((uint32_t)trackIdx, (uint32_t)sampleIdx, *pAudioFrame);
                    sample           = std::move(pAudioFrame);
                }
                else
                {
                    auto pSample = std::make_unique<Mp4RawSample>();
                    ret          = parser->getSample((uint32_t)trackIdx, (uint32_t)sampleIdx, *pSample);
                    sample       = std::move(pSample);
                }

                if (ret < 0)
                {
                    IMPORTANT_ERR("Get Track %zu Sample %zu error: %s\n", trackIdx, sampleIdx, parser->getErrorMessage().c_str());
                    return -1;
                }
                totalSize += sample->dataSize;
                samples.push_back(std::move(sample));
            }
            if (mBinaryData.bufferSize < totalSize)
            {
                mBinaryData.bufferSize = totalSize;
                mBinaryData.buffer     = std::make_unique<uint8_t[]>(mBinaryData.bufferSize);
            }

            size_t offset = 0;
            for (auto &pSample : samples)
            {
                memcpy(mBinaryData.buffer.get() + offset, pSample->sampleData.get(), pSample->dataSize);
                offset += pSample->dataSize;
            }
            mBinaryData.type     = 1;
            mBinaryData.trackIdx = trackIdx;
            mBinaryData.itemIdx  = itemIdx;
            break;
        }
        default:
            return -1;
    }

    return 0;
}

bool sampleTableClickable(const std::vector<Mp4SampleItem> &samples, size_t rowIdx, size_t colIdx)
{
    if (rowIdx >= samples.size())
        return false;

    if (colIdx == 0)
        return true;

    return false;
}
void Mp4ParserApp::saveCurrentData(const std::string &fileName, size_t size)
{
    FILE *fp = fopen(utf8ToLocal(fileName).c_str(), "wb");
    if (!fp)
    {
        IMPORTANT_ERR("Open %s error: %s\n", fileName.c_str(), getSystemError().c_str());
        return;
    }

    fwrite(mBinaryData.buffer.get(), 1, size, fp);
    fclose(fp);
    IMPORTANT_LOG("Save To %s Success\n", fileName.c_str());
}

void Mp4ParserApp::sampleTableClicked(size_t trackIdx, size_t rowIdx, size_t colIdx)
{
    UNUSED(colIdx);

    if (trackIdx >= getMp4DataShare().tracksInfo.size())
        return;

    auto &samples = getMp4DataShare().tracksInfo[trackIdx].mediaInfo->samplesInfo;
    if (rowIdx >= samples.size())
        return;

    Z_INFO("show Sample {}, offset {}, size {}\n", rowIdx, samples[rowIdx].sampleOffset, samples[rowIdx].sampleSize);
    if (updateData(0, trackIdx, rowIdx) < 0)
        return;

    mDataViewer.setUserData(&samples[rowIdx]);
    mDataViewer.setDataCallbacks(
        [](void *userData) -> ImS64
        {
            auto pSample = static_cast<Mp4SampleItem *>(userData);
            return pSample->sampleSize;
        },
        [this](ImS64 offset, void *userData) -> uint8_t
        {
            auto pSample = static_cast<Mp4SampleItem *>(userData);
            if (offset < 0 || offset >= (int64_t)pSample->sampleSize)
                return 0;
            return mBinaryData.buffer[offset];
        },
        [this](const string &fileName, void *userData)
        {
            auto pSample = static_cast<Mp4SampleItem *>(userData);
            saveCurrentData(fileName, pSample->sampleSize);
        });
    mDataViewer.open();
}

bool chunkTableClickable(const std::vector<Mp4ChunkItem> &chunks, size_t rowIdx, size_t colIdx)
{
    if (rowIdx >= chunks.size())
        return false;

    if (colIdx == 0)
        return true;

    return false;
}

void Mp4ParserApp::chunkTableClicked(size_t trackIdx, size_t rowIdx, size_t colIdx)
{
    UNUSED(colIdx);
    if (trackIdx >= getMp4DataShare().tracksInfo.size())
        return;

    auto &chunks = getMp4DataShare().tracksInfo[trackIdx].mediaInfo->chunksInfo;
    if (rowIdx >= chunks.size())
        return;

    Z_INFO("show Chunk {}, offset {}, size {}\n", rowIdx, chunks[rowIdx].chunkOffset, chunks[rowIdx].chunkSize);
    mDataViewer.setUserData(&chunks[rowIdx]);
    if (updateData(1, trackIdx, rowIdx) < 0)
        return;

    mDataViewer.setDataCallbacks(
        [](void *userData) -> ImS64
        {
            auto pChunk = static_cast<Mp4ChunkItem *>(userData);
            return pChunk->chunkSize;
        },
        [this](ImS64 offset, void *userData) -> uint8_t
        {
            auto pChunk = static_cast<Mp4ChunkItem *>(userData);
            if (offset < 0 || offset >= (int64_t)pChunk->chunkSize)
                return 0;
            return mBinaryData.buffer[offset];
        },
        [this](const string &fileName, void *userData)
        {
            auto pChunk = static_cast<Mp4ChunkItem *>(userData);
            saveCurrentData(fileName, pChunk->chunkSize);
        });
    mDataViewer.open();
}

void Mp4ParserApp::updateSamplesTable()
{
    mSampleDataTables.resize(getMp4DataShare().tracksInfo.size());

    for (size_t i = 0; i < mSampleDataTables.size(); i++)
    {
        auto &sampleTable = mSampleDataTables[i];
        sampleTable.setTableFlag(TABLE_FLAGS);
        sampleTable.clearColumns();
        sampleTable.addColumn("Idx").addColumn("Offset").addColumn("Size").addColumn("PTS(ms)");

        switch (getMp4DataShare().tracksInfo[i].trackType)
        {
            case TRACK_TYPE_VIDEO:
            {
                bool showFrameType = false;
                auto codecType     = mp4GetCodecType(getMp4DataShare().tracksInfo[i].mediaInfo->codecCode);
                if (MP4_CODEC_H264 == codecType || MP4_CODEC_H265 == codecType)
                    showFrameType = true;

                sampleTable.addColumn("DTS(ms)").addColumn("DTS Delta(ms)");
                if (showFrameType)
                    sampleTable.addColumn("Frame Type");
                sampleTable.addColumn("KeyFrame");

                sampleTable.setDataCallbacks(
                    [i]() { return getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo.size(); },
                    [i, showFrameType](size_t rowIdx, size_t colIdx) -> string
                    {
                        if (rowIdx >= getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo.size())
                            return "";
                        auto &curItem = getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo[rowIdx];
                        switch (colIdx)
                        {
                            default:
                                break;
                            case 0:
                                return to_string(curItem.sampleIdx);
                            case 1:
                                if (getAppConfigure().needShowInHex)
                                    return Log::format("{#x}", curItem.sampleOffset);
                                else
                                    return to_string(curItem.sampleOffset);
                            case 2:
                                if (getAppConfigure().needShowInHex)
                                    return Log::format("{#x}", curItem.sampleSize);
                                else
                                    return to_string(curItem.sampleSize);
                            case 3:
                                return to_string(curItem.ptsMs);
                            case 4:
                                return to_string(curItem.dtsMs);
                            case 5:
                                return to_string(curItem.dtsDeltaMs);
                        }
                        if (showFrameType)
                        {
                            if (colIdx == 6)
                            {
                                if (getAppConfigure().showRawFrameType)
                                {
                                    string str;
                                    for (size_t idx = 0; idx < curItem.naluTypes.size(); idx++)
                                    {
                                        str += to_string(curItem.naluTypes[idx]);
                                        if (idx < curItem.naluTypes.size() - 1)
                                        {
                                            str += ", ";
                                        }
                                    }
                                    return str;
                                }
                                else
                                {
                                    return mp4GetFrameTypeStr(curItem.frameType);
                                }
                            }
                            else if (colIdx == 7)
                                return curItem.isKeyFrame ? "True" : "False";
                        }
                        else
                        {
                            if (colIdx == 6)
                                return curItem.isKeyFrame ? "True" : "False";
                        }
                        return "";
                    },
                    std::bind(sampleTableClickable, ref(getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo),
                              std::placeholders::_1, std::placeholders::_2),
                    std::bind(&Mp4ParserApp::sampleTableClicked, this, i, std::placeholders::_1, std::placeholders::_2));
                break;
            }
            case TRACK_TYPE_AUDIO:
            {
                sampleTable.addColumn("PTS Delta(ms)");
                sampleTable.setDataCallbacks(
                    [i]() { return getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo.size(); },
                    [i](size_t rowIdx, size_t colIdx) -> string
                    {
                        if (rowIdx >= getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo.size())
                            return "";
                        auto &cur_item = getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo[rowIdx];
                        switch (colIdx)
                        {
                            case 0:
                                return to_string(cur_item.sampleIdx);
                            case 1:
                                if (getAppConfigure().needShowInHex)
                                    return Log::format("{#x}", cur_item.sampleOffset);
                                else
                                    return to_string(cur_item.sampleOffset);
                            case 2:
                                if (getAppConfigure().needShowInHex)
                                    return Log::format("{#x}", cur_item.sampleSize);
                                else
                                    return to_string(cur_item.sampleSize);
                            case 3:
                                return to_string(cur_item.ptsMs);
                            case 4:
                                return to_string(cur_item.dtsDeltaMs);
                            default:
                                return "";
                        }
                    },
                    std::bind(sampleTableClickable, ref(getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo),
                              std::placeholders::_1, std::placeholders::_2),
                    std::bind(&Mp4ParserApp::sampleTableClicked, this, i, std::placeholders::_1, std::placeholders::_2));
                break;
            }
            default:
            {
                sampleTable.setDataCallbacks(
                    [i]() { return getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo.size(); },
                    [i](size_t rowIdx, size_t colIdx) -> string
                    {
                        if (rowIdx >= getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo.size())
                            return "";
                        auto &cur_item = getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo[rowIdx];
                        switch (colIdx)
                        {
                            case 0:
                                return to_string(cur_item.sampleIdx);
                            case 1:
                                if (getAppConfigure().needShowInHex)
                                    return Log::format("{#x}", cur_item.sampleOffset);
                                else
                                    return to_string(cur_item.sampleOffset);
                            case 2:
                                if (getAppConfigure().needShowInHex)
                                    return Log::format("{#x}", cur_item.sampleSize);
                                else
                                    return to_string(cur_item.sampleSize);
                            case 3:
                                return to_string(cur_item.ptsMs);
                            default:
                                return "";
                        }
                    },
                    std::bind(sampleTableClickable, ref(getMp4DataShare().tracksInfo[i].mediaInfo->samplesInfo),
                              std::placeholders::_1, std::placeholders::_2),
                    std::bind(&Mp4ParserApp::sampleTableClicked, this, i, std::placeholders::_1, std::placeholders::_2));
                break;
            }
        }
    }
}

void Mp4ParserApp::updateChunksTable()
{
    if (cur_track_select >= (int)getMp4DataShare().tracksInfo.size())
        return;
    mChunkDataTables.resize(getMp4DataShare().tracksInfo.size());
    for (size_t i = 0; i < mChunkDataTables.size(); i++)
    {
        auto &chunkTable = mChunkDataTables[i];
        chunkTable.setTableFlag(TABLE_FLAGS);
        chunkTable.clearColumns();
        chunkTable.addColumn("Idx");
        chunkTable.addColumn("Offset");
        chunkTable.addColumn("Size");
        chunkTable.addColumn("Sample Start");
        chunkTable.addColumn("Sample Count");
        chunkTable.addColumn("Start PTS(ms)");
        chunkTable.addColumn("Delta(ms)");
        chunkTable.addColumn("Avg Bitrate(Kbps)");
        chunkTable.setDataCallbacks(
            [i]() { return getMp4DataShare().tracksInfo[i].mediaInfo->chunksInfo.size(); },
            [i](size_t rowIdx, size_t colIdx) -> string
            {
                if (rowIdx >= getMp4DataShare().tracksInfo[i].mediaInfo->chunksInfo.size())
                    return "";
                auto &cur_item = getMp4DataShare().tracksInfo[i].mediaInfo->chunksInfo[rowIdx];
                switch (colIdx)
                {
                    case 0:
                        return to_string(cur_item.chunkIdx);
                    case 1:
                        if (getAppConfigure().needShowInHex)
                            return Log::format("{#x}", cur_item.chunkOffset);
                        else
                            return to_string(cur_item.chunkOffset);
                    case 2:
                        if (getAppConfigure().needShowInHex)
                            return Log::format("{#x}", cur_item.chunkSize);
                        else
                            return to_string(cur_item.chunkSize);
                    case 3:
                        return to_string(cur_item.sampleStartIdx);
                    case 4:
                        return to_string(cur_item.sampleCount);
                    case 5:
                        return to_string(cur_item.startPtsMs);
                    case 6:
                        return to_string(cur_item.durationMs);
                    case 7:
                        return to_string(cur_item.avgBitrateBps / 1024.f);
                    default:
                        return "";
                }
            },
            std::bind(chunkTableClickable, ref(getMp4DataShare().tracksInfo[i].mediaInfo->chunksInfo), std::placeholders::_1,
                      std::placeholders::_2),
            std::bind(&Mp4ParserApp::chunkTableClicked, this, i, std::placeholders::_1, std::placeholders::_2));
    }
}

void Mp4ParserApp::showMp4InfoTab()
{
    mDockId = ImGui::GetID("Mp4InfoDockSpace");
    ImGui::DockSpace(mDockId, {0, 0}, mDockSpaceFlags);

    splitDock(mDockId, ImGuiDir_Left, 0.3f, &mBoxTreeDock, &mBoxInfoDock);

    ImGui::SetNextWindowDockID(mBoxTreeDock, ImGuiCond_FirstUseEver);
    ImGui::Begin("Leading");
    ImGui::BeginTabBar("Leadings", ImGuiTabBarFlags_FittingPolicyResizeDown);

    if (ImGui::IsKeyPressed(ImGuiKey_F) && m_focus_on == FOCUS_ON_BOXES)
        set_all_open_state(mVirtFileBox.get(), mSomeTreeNodeOpened);

    ShowBoxesTreeView();
    ShowTracksTreeView();
    ImGui::EndTabBar();
    ImGui::End();

    ImGui::SetNextWindowDockID(mBoxInfoDock, ImGuiCond_FirstUseEver);
    mInfoWindow.show();

    static int start_phase = 1;
    if (start_phase)
    {
        Z_INFO("set focus\n");
        ImGui::SetWindowFocus("Boxes");
        start_phase = 0;
    }
}

Mp4ParserApp::Mp4ParserApp() : mInfoWindow("Information")
{
    openDebugWindow();
    Log::set_log_level(LOG_LEVEL_INFO);

    enableStatusBar(true);
    setStatus("Ready to go\n");

    addSetting(
        SettingValue::SettingBool, "ShowInHex", [](const void *val) { getAppConfigure().needShowInHex = *(bool *)val; },
        [](void *val) { *(bool *)val = getAppConfigure().needShowInHex; });
    addSetting(
        SettingValue::SettingBool, "Logarithmic Axis", [](const void *val) { getAppConfigure().logarithmicAxis = *(bool *)val; },
        [](void *val) { *(bool *)val = getAppConfigure().logarithmicAxis; });
    addSetting(
        SettingValue::SettingInt, "Hardware Decode First", [](const void *val)
        { getAppConfigure().hardwareDecode = *(int *)val; }, [](void *val) { *(int *)val = getAppConfigure().hardwareDecode; });
    addSetting(
        SettingValue::SettingBool, "Show Debug Log", [](const void *val) { getAppConfigure().needShowDebugLog = *(bool *)val; },
        [](void *val) { *(bool *)val = getAppConfigure().needShowDebugLog; });
    addSetting(
        SettingValue::SettingBool, "Show Wrapped Data", [](const void *val) { getAppConfigure().showWrappedData = *(bool *)val; },
        [](void *val) { *(bool *)val = getAppConfigure().showWrappedData; });

    addMenu({"Menu"});
    addMenu({"Settings"});
    addMenu({"Menu", "Open File"},
            [this]()
            {
                vector<FilterSpec> filter = {
                    {"*.mp4;*.MP4", "Mp4"}
                };
                string initDir;
                if (!getMp4DataShare().curFilePath.empty())
                {
                    fs::path filePath(utf8ToLocal(getMp4DataShare().curFilePath));
                    if (fs::exists(filePath))
                    {
                        initDir = filePath.parent_path().string();
                        Z_INFO("initDir: {}\n", initDir);
                    }
                }
                string filePath = selectFile(filter, initDir);
                if (filePath.empty())
                {
                    auto err = getLastError();
                    if (!err.empty())
                    {
                        IMPORTANT_ERR("Get Open File Fail: %s", err.c_str());
                    }
                }
                else
                {
                    Z_INFO("Selected filename {}\n", filePath);
                    startParseFile(filePath);
                }
            });

    addMenu({"Settings", "Show Offset/Size in Hex"}, nullptr, &getAppConfigure().needShowInHex);
    addMenu({"Settings", "Binary View"}, nullptr, &getAppConfigure().showBoxBinaryData);
    addMenu({"Settings", "Logarithmic Axis"}, nullptr, &getAppConfigure().logarithmicAxis);

    addMenu(
        {"Settings", "Hardware Decode", "Off"},
        []()
        {
            getAppConfigure().hardwareDecode = -1;
            Z_INFO("HW Decode: {}\n", getAppConfigure().hardwareDecode);
            getMp4DataShare().recreateDecoder();
        },
        []() { return -1 == getAppConfigure().hardwareDecode; });
    addMenu(
        {"Settings", "Hardware Decode", "Auto"},
        []()
        {
            getAppConfigure().hardwareDecode = 0;
            Z_INFO("HW Decode: {}\n", getAppConfigure().hardwareDecode);
            getMp4DataShare().recreateDecoder();
        },
        []() { return 0 == getAppConfigure().hardwareDecode; });

    std::vector<AVHWDeviceType> hwTypes = getSupportHWDeviceType();
    for (auto type : hwTypes)
    {
        Z_INFO("Support {}\n", type);
        addMenu(
            {"Settings", "Hardware Decode", av_hwdevice_get_type_name(type)},
            [type]()
            {
                getAppConfigure().hardwareDecode = type;
                Z_INFO("HW Decode: {}\n", getAppConfigure().hardwareDecode);
                getMp4DataShare().recreateDecoder();
            },
            [type]() { return type == getAppConfigure().hardwareDecode; });
    }

    addMenu({"Info", "Frame Info"}, nullptr, &getAppConfigure().needShowFrameInfo);
    addMenu({"Info", "More Debug Log"}, nullptr, &getAppConfigure().needShowDebugLog);

    getMp4DataShare().onFrameParsed = [this](unsigned int trackIdx, int frameIdx, H26X_FRAME_TYPE_E frameType)
    {
        string name = mp4GetFrameTypeStr(frameType);
        mVideoStreamInfo.updateFrameInfo(trackIdx, frameIdx, frameType);
    };

    mBoxBinaryViewer.setDataCallbacks(getBoxSize, getBoxData, SaveBoxData);

    mInfoWindow.setContent([&]() { ShowInfoView(); });
    mInfoWindow.removeHoveredFlag(ImGuiHoveredFlags_ChildWindows);
    mInfoWindow.open();
}

Mp4ParserApp::~Mp4ParserApp() {}

void Mp4ParserApp::WrapDatacheckBox()
{
    if (ImGui::Checkbox("Show Wrapped Data", &getAppConfigure().showWrappedData))
    {
        updateData(mBinaryData.type, mBinaryData.trackIdx, mBinaryData.itemIdx);
    }
    ImGui::SetItemTooltip("If this is checked:\n"
                          "SPS/PPS/VPS... and start code will be added to video data(H264/H265);\n"
                          "Adts header will be added to audio data(AAC).\n");

    ImGui::Checkbox("Show Raw Frame Type", &getAppConfigure().showRawFrameType);
}
void Mp4ParserApp::ShowInfoView()
{
    if (m_focus_changed)
    {
        Z_INFO("Focus on {}, select {}:{}\n", m_focus_on, cur_box_select ? cur_box_select->box_type : "null", cur_track_select);
        m_focus_changed = false;
    }

    if (FOCUS_ON_BOXES == m_focus_on)
    {
        if (cur_box_select)
        {
            if (getAppConfigure().showBoxBinaryData)
            {
                mBoxBinaryViewer.show();
                string err = mBoxBinaryViewer.getError();
                if (!err.empty())
                {
                    setStatus(err, ERR_COLOR);
                }
                while (!err.empty())
                {
                    ADD_LOG("%s\n", err.c_str());
                    err = mBoxBinaryViewer.getError();
                }
            }
            else
            {
                for (size_t item_idx = 0; item_idx < cur_box_select->pdata->size(); item_idx++)
                {
                    string key   = cur_box_select->pdata->kvGetKey(item_idx);
                    auto   value = cur_box_select->pdata->kvGetValue(key);
                    ShowInfoItem(cur_box_select->box_index, key, *value);
                }
            }
        }
    }
    else if (FOCUS_ON_TRACKS == m_focus_on)
    {
        if (cur_track_select >= 0)
        {
            if (cur_track_select < (int)getMp4DataShare().tracksInfo.size())
            {
                ImGui::BeginTabBar("Informations", ImGuiTabBarFlags_FittingPolicyResizeDown);

                if (ImGui::BeginTabItem("Sample Info"))
                {
                    WrapDatacheckBox();
                    mSampleDataTables[cur_track_select].show();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Chunk Info"))
                {
                    WrapDatacheckBox();
                    mChunkDataTables[cur_track_select].show();

                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
    }
}
void Mp4ParserApp::transferCmdArgs(std::vector<std::string> &args)
{
    if (args.size() > 1)
    {
        startParseFile(args[1]);
    }
}

void Mp4ParserApp::dropFile(const vector<string> &filesPath)
{
    if (filesPath.empty())
        return;

    startParseFile(filesPath[0]);
}

void Mp4ParserApp::presetInternal()
{
    mWindowRect      = {100, 100, 640, 480};
    mApplicationName = "Mp4 Parser";
    ImGuiIO &io      = ImGui::GetIO();
    if (!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable))
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    getMp4DataShare().init(
        [&](MP4_LOG_LEVEL_E logLevel, const char *str)
        {
            string tmpStr = str;
            if (tmpStr.back() != '\n')
                tmpStr.append("\n");

            switch (logLevel)
            {
                case MP4_LOG_LEVEL_ERR:
                    tmpStr = gColorStrMap[ColorRed] + tmpStr + gColorStrMap[ColorNone];
                    break;
                case MP4_LOG_LEVEL_WARN:
                    tmpStr = gColorStrMap[ColorYellow] + tmpStr + gColorStrMap[ColorNone];
                    break;
                case MP4_LOG_LEVEL_INFO:
                    tmpStr = gColorStrMap[ColorGreen] + tmpStr + gColorStrMap[ColorNone];
                    break;
                default:
                case MP4_LOG_LEVEL_DBG:
                    if (!getAppConfigure().needShowDebugLog)
                        return;
                    break;
            }

            addLog(tmpStr);
        });

    Z_INFO("ffmpeg version: {}\n", av_version_info());

    setTitle(mApplicationName);
}

bool Mp4ParserApp::renderUI()
{
    if (getMp4DataShare().isRunning())
    {
        if (OPERATION_PARSE_FILE == getMp4DataShare().getCurrentOperation())
            setStatusProgressBar(true, getMp4DataShare().getParseFileProgress());
        else if (OPERATION_PARSE_FRAME_TYPE == getMp4DataShare().getCurrentOperation())
            setStatusProgressBar(true, getMp4DataShare().getParseFrameTypeProgress());

        Z_INFO("Wait for parsing\n");
    }
    if (MyThread::STATE_FINISHED == getMp4DataShare().getState())
    {
        getMp4DataShare().stop();
        if (OPERATION_PARSE_FILE == getMp4DataShare().getCurrentOperation())
        {
            if (getMp4DataShare().newDataAvailable)
            {
                resetFileInfo();
                string   filePathStr = getMp4DataShare().getParser()->getFilePath();
                fs::path filePath(filePathStr); // use local encode file path, not utf8
                string   newTitle = filePath.filename().string();
                newTitle          = localToUtf8(newTitle);
                setTitle(newTitle);
                setApplicationTitle(newTitle);

                getMp4DataShare().startParse(OPERATION_PARSE_FRAME_TYPE);
                setStatus(combineString("Parse ", getProperFilePathForStatus(getMp4DataShare().curFilePath),
                                        " Success, Extracting type of every frame..."));
            }
            else
            {
                setStatus(combineString("Parse " + getProperFilePathForStatus(getMp4DataShare().toParseFilePath) + " Fail"),
                          ERR_COLOR);
                setStatusProgressBar(false);
            }
        }
        else if (OPERATION_PARSE_FRAME_TYPE == getMp4DataShare().getCurrentOperation())
        {
            if (getMp4DataShare().dataAvailable)
            {
                setStatus(combineString("Parse " + getProperFilePathForStatus(getMp4DataShare().curFilePath)) + " Success");
            }
            setStatusProgressBar(false);
        }
    }

    ImGui::BeginTabBar("Different Infos", ImGuiTabBarFlags_FittingPolicyResizeDown);

    if (ImGui::BeginTabItem("Mp4Info"))
    {
        showMp4InfoTab();
        mDataViewer.show();
        ImGui::EndTabItem();
    }

    if (!getMp4DataShare().mVideoTracksIdx.empty() && ImGui::BeginTabItem("StreamInfo"))
    {
        mVideoStreamInfo.show();

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();

    if (m_metrics_show)
        ImGui::ShowMetricsWindow(&m_metrics_show);

    return justClosed();
}

void Mp4ParserApp::updateBoxDataTables(uint64_t boxIdx, const string &key, const Mp4BoxData &value)
{
    if (MP4_BOX_DATA_TYPE_KEY_VALUE_PAIRS == value.getDataType())
    {
        for (size_t item_idx = 0; item_idx < value.size(); item_idx++)
        {
            string subKey   = value.kvGetKey(item_idx);
            auto   subValue = value.kvGetValue(key);
            updateBoxDataTables(boxIdx, subKey, *subValue);
        }
    }
    else if (MP4_BOX_DATA_TYPE_TABLE == value.getDataType())
    {
        ImGuiItemTable table(key + "Table##" + std::to_string(boxIdx));
        table.setTableFlag(TABLE_FLAGS);
        for (size_t header_idx = 0; header_idx < value.tableGetColumnCount(); header_idx++)
        {
            table.addColumn(value.tableGetColumnName(header_idx));
        }
        table.setDataCallbacks([&]() { return value.size(); },
                               [&](size_t rowIdx, size_t colIdx) -> string
                               {
                                   if (rowIdx >= value.size())
                                       return "";
                                   auto cur_item = value.tableGetRow(rowIdx);
                                   if (getAppConfigure().needShowInHex
                                       && (value.tableGetColumnName(colIdx).find("Offset") != string::npos
                                           || value.tableGetColumnName(colIdx).find("Size") != string::npos))
                                       return cur_item->arrayGetData(colIdx)->toHexString();
                                   else
                                       return cur_item->arrayGetData(colIdx)->toString();
                               });

        mBoxInfoTables[&value] = table;
    }
}
void Mp4ParserApp::updateBoxDataTables(const BoxInfo &box)
{
    for (uint64_t item_idx = 0; item_idx < box.pdata->size(); item_idx++)
    {
        string key   = box.pdata->kvGetKey(item_idx);
        auto  &value = *box.pdata->kvGetValue(key);
        updateBoxDataTables(box.box_index, key, value);
    }
    for (auto &sub_box : box.sub_list)
    {
        updateBoxDataTables(*sub_box.get());
    }
}

void createBinaryViewer(BoxInfo *pBoxInfo, const string &key, const Mp4BoxData *pData)
{
    if (pData->getDataType() == MP4_BOX_DATA_TYPE_BINARY)
    {
        auto title = std::to_string(pBoxInfo->box_index) + " " + pBoxInfo->box_type + key;
        if (pBoxInfo->binaryValueViewers.find(key) != pBoxInfo->binaryValueViewers.end())
        {
            ADD_LOG("duplicate key: %s for box %s(%d_\n", key.c_str(), pBoxInfo->box_type.c_str(), pBoxInfo->box_index);
            return;
        }

        auto newViewer = std::make_shared<ImGuiBinaryViewer>(title, true);
        newViewer->setSize(ImVec2(0, 150));
        newViewer->setUserData((void *)pData);
        newViewer->setDataCallbacks(
            [](const void *userData)
            {
                auto pData = (Mp4BoxData *)userData;
                return pData->size();
            },
            [](uint64_t offset, const void *userData) -> uint8_t
            {
                auto pData = (Mp4BoxData *)userData;
                if (offset >= pData->size())
                    return 0;
                return pData->binaryGetData(offset);
            },
            [](const std::string &filePath, const void *userData)
            {
                auto     pData = (Mp4BoxData *)userData;
                uint64_t size  = pData->binaryGetSize();

                FILE *fp = fopen(filePath.c_str(), "wb+");
                if (fp == nullptr)
                {
                    IMPORTANT_ERR("Open %s Fail: %s", localToUtf8(filePath).c_str(), getLastError().c_str());
                    return;
                }
                for (uint64_t i = 0; i < size; i++)
                {
                    uint8_t data = pData->binaryGetData(i);
                    fwrite(&data, sizeof(uint8_t), 1, fp);
                }
                fclose(fp);
                IMPORTANT_LOG("Save To %s Success\n", localToUtf8(filePath).c_str());
            });
        pBoxInfo->binaryValueViewers.insert(std::make_pair(key, newViewer));
    }
    else if (pData->getDataType() == MP4_BOX_DATA_TYPE_KEY_VALUE_PAIRS)
    {
        for (size_t item_idx = 0; item_idx < pData->size(); item_idx++)
        {
            string subKey   = pData->kvGetKey(item_idx);
            auto   subValue = pData->kvGetValue(subKey);
            createBinaryViewer(pBoxInfo, subKey, subValue.get());
        }
    }
    else if (pData->getDataType() == MP4_BOX_DATA_TYPE_ARRAY)
    {
        for (size_t item_idx = 0; item_idx < pData->size(); item_idx++)
        {
            auto subValue = pData->arrayGetData(item_idx);
            createBinaryViewer(pBoxInfo, combineString(key, "[", item_idx, "]"), subValue.get());
        }
    }
}

shared_ptr<BoxInfo> Mp4ParserApp::getBoxInfo(const Mp4Box *pBox, int layer, int &boxCount)
{
    shared_ptr<BoxInfo> boxInfo = std::make_shared<BoxInfo>();

    boxInfo->layer     = layer;
    boxInfo->box_index = boxCount++;
    boxInfo->box_type  = pBox->getBoxTypeStr();
    boxInfo->pdata     = pBox->getData();

    boxInfo->boxPosition = pBox->getBoxPos();
    boxInfo->boxSize     = pBox->getBoxSize();

    createBinaryViewer(boxInfo.get(), "", boxInfo->pdata.get());

    mAllBoxes.push_back(boxInfo.get());
    auto boxes = pBox->getSubBoxes();
    for (auto &sub_box : boxes)
    {
        auto subBoxInfo = getBoxInfo(sub_box.get(), layer + 1, boxCount);
        boxInfo->sub_list.push_back(subBoxInfo);
    }
    return boxInfo;
}

void Mp4ParserApp::resetFileInfo()
{
    cur_box_select   = nullptr;
    cur_track_select = -1;

    getMp4DataShare().updateData();

    // update box table data
    int  boxCount          = 0;
    auto top               = getMp4DataShare().getParser()->asBox();
    mVirtFileBox           = getBoxInfo(top.get(), 0, boxCount);
    mVirtFileBox->box_type = localToUtf8(mVirtFileBox->box_type);

    mBoxInfoTables.clear();
    updateBoxDataTables(*mVirtFileBox);

    cur_box_select = mVirtFileBox.get();
    mBoxBinaryViewer.setUserData(mVirtFileBox.get());
    cur_track_select = 0;

    // update imgui items
    updateSamplesTable();
    updateChunksTable();

    if (!getMp4DataShare().mVideoTracksIdx.empty())
    {
        mVideoStreamInfo.resetData();
    }
}

void Mp4ParserApp::startParseFile(const std::string &file_path)
{
    if (getMp4DataShare().isRunning())
    {
        getMp4DataShare().stop();
    }

    getMp4DataShare().toParseFilePath = file_path;
    if (getMp4DataShare().startParse(OPERATION_PARSE_FILE) < 0)
    {
        IMPORTANT_ERR("Parse %s Fail", getProperFilePathForStatus(file_path).c_str());
        return;
    }
    setStatus(Log::format("Parsing {}...\n", getProperFilePathForStatus(file_path)));
}
void Mp4ParserApp::exit()
{
    if (getMp4DataShare().isRunning())
    {
        getMp4DataShare().stop();
    }
}
