/**
 * @file streamingaudio_fmodstudio.cpp
 * @brief LLStreamingAudio_FMODSTUDIO implementation
 *
 * $LicenseInfo:firstyear=2020&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2020, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llstreamingaudio_fmodstudio.h"

#include "llsd.h"
#include "llmath.h"
#include "llmutex.h"

#include "fmodstudio/fmod.hpp"
#include "fmodstudio/fmod_errors.h"

inline bool Check_FMOD_Stream_Error(FMOD_RESULT result, const char *string)
{
    if (result == FMOD_OK)
        return false;
    LL_WARNS("FMOD") << string << " Error: " << FMOD_ErrorString(result) << LL_ENDL;
    return true;
}

class LLAudioStreamManagerFMODSTUDIO
{
public:
    LLAudioStreamManagerFMODSTUDIO(FMOD::System *system, FMOD::ChannelGroup *group, const std::string& url);
    FMOD::Channel* startStream();
    bool stopStream(); // Returns true if the stream was successfully stopped.
    bool ready();

    const std::string& getURL()     { return mInternetStreamURL; }

    FMOD_OPENSTATE getOpenState(unsigned int* percentbuffered = NULL, bool* starving = NULL, bool* diskbusy = NULL);
protected:
    FMOD::System* mSystem;
    FMOD::ChannelGroup* mChannelGroup;
    FMOD::Channel* mStreamChannel;
    FMOD::Sound* mInternetStream;
    bool mReady;

    std::string mInternetStreamURL;
};

LLMutex gWaveDataMutex;    //Just to be extra strict.
const U32 WAVE_BUFFER_SIZE = 1024;
U32 gWaveBufferMinSize = 0;
F32 gWaveDataBuffer[WAVE_BUFFER_SIZE] = { 0.f };
U32 gWaveDataBufferSize = 0;

FMOD_RESULT F_CALL waveDataCallback(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels)
{
    if (!length || !inchannels)
        return FMOD_OK;
    memcpy(outbuffer, inbuffer, length * inchannels * sizeof(float));

    static std::vector<F32> local_buf;
    if (local_buf.size() < length)
        local_buf.resize(length, 0.f);

    for (U32 i = 0; i < length; ++i)
    {
        F32 total = 0.f;
        for (S32 j = 0; j < inchannels; ++j)
        {
            total += inbuffer[i*inchannels + j];
        }
        local_buf[i] = total / inchannels;
    }

    {
        LLMutexLock lock(&gWaveDataMutex);

        for (U32 i = length; i > 0; --i)
        {
            if (++gWaveDataBufferSize > WAVE_BUFFER_SIZE)
            {
                if (gWaveBufferMinSize)
                    memcpy(gWaveDataBuffer + WAVE_BUFFER_SIZE - gWaveBufferMinSize, gWaveDataBuffer, gWaveBufferMinSize * sizeof(float));
                gWaveDataBufferSize = 1 + gWaveBufferMinSize;
            }
            gWaveDataBuffer[WAVE_BUFFER_SIZE - gWaveDataBufferSize] = local_buf[i - 1];
        }
    }

    return FMOD_OK;
}

//---------------------------------------------------------------------------
// Internet Streaming
//---------------------------------------------------------------------------
LLStreamingAudio_FMODSTUDIO::LLStreamingAudio_FMODSTUDIO(FMOD::System *system) :
mSystem(system),
mCurrentInternetStreamp(nullptr),
mStreamDSP(nullptr),
mStreamGroup(nullptr),
mFMODInternetStreamChannelp(nullptr),
mGain(1.0f),
mRetryCount(0),
mMetadata(LLSD::emptyMap())
{
    // Number of milliseconds of audio to buffer for the audio card.
    // Must be larger than the usual Second Life frame stutter time.
    const U32 buffer_seconds = 10;      //sec
    const U32 estimated_bitrate = 128;  //kbit/sec
    Check_FMOD_Stream_Error(mSystem->setStreamBufferSize(estimated_bitrate * buffer_seconds * 128/*bytes/kbit*/, FMOD_TIMEUNIT_RAWBYTES), "FMOD::System::setStreamBufferSize");

    Check_FMOD_Stream_Error(system->createChannelGroup("stream", &mStreamGroup), "FMOD::System::createChannelGroup");

    FMOD_DSP_DESCRIPTION dspdesc = { };
    dspdesc.pluginsdkversion = FMOD_PLUGIN_SDK_VERSION;
    strncpy(dspdesc.name, "Waveform", sizeof(dspdesc.name));
    dspdesc.numoutputbuffers = 1;
    dspdesc.read = &waveDataCallback; //Assign callback.

    Check_FMOD_Stream_Error(system->createDSP(&dspdesc, &mStreamDSP), "FMOD::System::createDSP");
}


LLStreamingAudio_FMODSTUDIO::~LLStreamingAudio_FMODSTUDIO()
{
    if (mCurrentInternetStreamp)
    {
        // Isn't supposed to hapen, stream should be clear by now,
        // and if it does, we are likely going to crash.
        LL_WARNS("FMOD") << "mCurrentInternetStreamp not null on shutdown!" << LL_ENDL;
        stop();
    }

    // Kill dead internet streams, if possible
    killDeadStreams();

    if (!mDeadStreams.empty())
    {
        // LLStreamingAudio_FMODSTUDIO was inited on startup
        // and should be destroyed on shutdown, it should
        // wait for streams to die to not cause crashes or
        // leaks.
        // Ideally we need to wait on some kind of callback
        // to release() streams correctly, but 200 ms should
        // be enough and we can't wait forever.
        LL_INFOS("FMOD") << "Waiting for " << (S32)mDeadStreams.size() << " streams to stop" << LL_ENDL;
        for (S32 i = 0; i < 20; i++)
        {
            const U32 ms_delay = 10;
            ms_sleep(ms_delay); // rude, but not many options here
            killDeadStreams();
            if (mDeadStreams.empty())
            {
                LL_INFOS("FMOD") << "All streams stopped after " << (S32)((i + 1) * ms_delay) << "ms" << LL_ENDL;
                break;
            }
        }
    }

    if (!mDeadStreams.empty())
    {
        LL_WARNS("FMOD") << "Failed to kill some audio streams" << LL_ENDL;
    }

    cleanupWaveData();
}

void LLStreamingAudio_FMODSTUDIO::killDeadStreams()
{
    std::list<LLAudioStreamManagerFMODSTUDIO *>::iterator iter;
    for (iter = mDeadStreams.begin(); iter != mDeadStreams.end();)
    {
        LLAudioStreamManagerFMODSTUDIO *streamp = *iter;
        if (streamp->stopStream())
        {
            LL_INFOS("FMOD") << "Closed dead stream" << LL_ENDL;
            delete streamp;
            iter = mDeadStreams.erase(iter);
        }
        else
        {
            iter++;
        }
    }
}

void LLStreamingAudio_FMODSTUDIO::start(const std::string& url)
{
    //if (!mInited)
    //{
    //  LL_WARNS() << "startInternetStream before audio initialized" << LL_ENDL;
    //  return;
    //}

    // "stop" stream but don't clear url, etc. in case url == mInternetStreamURL
    stop();

    if (!url.empty())
    {
        LL_INFOS("FMOD") << "Starting internet stream: " << url << LL_ENDL;
        mCurrentInternetStreamp = new LLAudioStreamManagerFMODSTUDIO(mSystem, mStreamGroup, url);
        mURL = url;
    }
    else
    {
        LL_INFOS("FMOD") << "Set internet stream to null" << LL_ENDL;
        mURL.clear();
    }

    mRetryCount = 0;
}

enum utf_endian_type_t
{
    UTF16LE,
    UTF16BE,
    UTF16
};

std::string utf16input_to_utf8(unsigned char* input, U32 len, utf_endian_type_t type)
{
    if (type == UTF16)
    {
        type = UTF16BE;    //Default
        if (len > 2)
        {
            //Parse and strip BOM.
            if ((input[0] == 0xFE && input[1] == 0xFF) ||
                (input[0] == 0xFF && input[1] == 0xFE))
            {
                input += 2;
                len -= 2;
                type = input[0] == 0xFE ? UTF16BE : UTF16LE;
            }
        }
    }
    llutf16string out_16((llutf16string::value_type*)input, len / 2);
    if (len % 2)
    {
        out_16.push_back((input)[len - 1] << 8);
    }
    if (type == UTF16BE)
    {
        for (llutf16string::iterator i = out_16.begin(); i < out_16.end(); ++i)
        {
            llutf16string::value_type v = *i;
            *i = ((v & 0x00FF) << 8) | ((v & 0xFF00) >> 8);
        }
    }
    return utf16str_to_utf8str(out_16);
}

void LLStreamingAudio_FMODSTUDIO::update()
{
    // Kill dead internet streams, if possible
    killDeadStreams();

    // Don't do anything if there are no streams playing
    if (!mCurrentInternetStreamp)
    {
        return;
    }

    unsigned int progress;
    bool starving;
    bool diskbusy;
    FMOD_OPENSTATE open_state = mCurrentInternetStreamp->getOpenState(&progress, &starving, &diskbusy);

    if (open_state == FMOD_OPENSTATE_READY)
    {
        // Stream is live

        // start the stream if it's ready
        if (!mFMODInternetStreamChannelp &&
            (mFMODInternetStreamChannelp = mCurrentInternetStreamp->startStream()))
        {
            // Reset volume to previously set volume
            setGain(getGain());
            if (mStreamDSP)
            {
                Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, mStreamDSP), "FMOD::Channel::addDSP");
            }
            Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPaused(false), "FMOD::Channel::setPaused");
        }
        mRetryCount = 0;
    }
    else if (open_state == FMOD_OPENSTATE_ERROR)
    {
        LL_INFOS("FMOD") << "State: FMOD_OPENSTATE_ERROR"
            << " Progress: " << U32(progress)
            << " Starving: " << S32(starving)
            << " Diskbusy: " << S32(diskbusy) << LL_ENDL;
        if (mRetryCount < 2)
        {
            // Retry
            std::string url = mURL;
            stop(); // might drop mURL, drops mCurrentInternetStreamp

            mRetryCount++;

            if (!url.empty())
            {
                LL_INFOS("FMOD") << "Restarting internet stream: " << url  << ", attempt " << (mRetryCount + 1) << LL_ENDL;
                mCurrentInternetStreamp = new LLAudioStreamManagerFMODSTUDIO(mSystem, mStreamGroup, url);
                mURL = url;
            }
        }
        else
        {
            stop();
        }
        return;
    }

    if (mFMODInternetStreamChannelp)
    {
        FMOD::Sound *sound = nullptr;

        if (mFMODInternetStreamChannelp->getCurrentSound(&sound) == FMOD_OK && sound)
        {
            FMOD_TAG tag;
            S32 tagcount, dirtytagcount;

            if (sound->getNumTags(&tagcount, &dirtytagcount) == FMOD_OK && dirtytagcount > 0)
            {
                mMetadata = LLSD::emptyMap();

                for (S32 i = 0; i < tagcount; ++i)
                {
                    if (sound->getTag(nullptr, i, &tag) != FMOD_OK)
                        continue;

                    std::string name = tag.name;
                    switch (tag.type)
                    {
                        case FMOD_TAGTYPE_ID3V2:
                        {
                            if (!LLStringUtil::compareInsensitive(name, "TIT2")) name = "TITLE";
                            else if(!LLStringUtil::compareInsensitive(name, "TPE1")) name = "ARTIST";
                            break;
                        }
                        case FMOD_TAGTYPE_ASF:
                        {
                            if (!LLStringUtil::compareInsensitive(name, "Title")) name = "TITLE";
                            else if (!LLStringUtil::compareInsensitive(name, "WM/AlbumArtist")) name = "ARTIST";
                            break;
                        }
                        case FMOD_TAGTYPE_VORBISCOMMENT:
                        {
                                if (!LLStringUtil::compareInsensitive(name, "title")) name = "TITLE";
                                else if (!LLStringUtil::compareInsensitive(name, "artist")) name = "ARTIST";
                            break;
                        }
                        case FMOD_TAGTYPE_FMOD:
                        {
                            if (!LLStringUtil::compareInsensitive(name, "Sample Rate Change"))
                            {
                                LL_INFOS("FMOD") << "Stream forced changing sample rate to " << *((float *)tag.data) << LL_ENDL;
                                Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setFrequency(*((float *)tag.data)), "FMOD::Channel::setFrequency");
                            }
                            continue;
                        }
                        default:
                            if (!LLStringUtil::compareInsensitive(name, "TITLE") ||
                                !LLStringUtil::compareInsensitive(name, "ARTIST"))
                                LLStringUtil::toUpper(name);
                            break;
                    }
                    switch (tag.datatype)
                    {
                        case(FMOD_TAGDATATYPE_INT):
                           mMetadata[name]=*(LLSD::Integer*)(tag.data);
                            LL_INFOS("FMOD") << tag.name << ": " << *(int*)(tag.data) << LL_ENDL;
                            break;
                        case(FMOD_TAGDATATYPE_FLOAT):
                            mMetadata[name]=*(LLSD::Real*)(tag.data);
                            LL_INFOS("FMOD") << tag.name << ": " << *(float*)(tag.data) << LL_ENDL;
                            break;
                        case(FMOD_TAGDATATYPE_STRING):
                        {
                            std::string out = rawstr_to_utf8(std::string((char*)tag.data,tag.datalen));
                            if (out.length() && out[out.size() - 1] == 0)
                                out.erase(out.size() - 1);
                            mMetadata[name]=out;
                            LL_INFOS("FMOD") << tag.name << "(RAW): " << out << LL_ENDL;
                            break;
                        }
                        case(FMOD_TAGDATATYPE_STRING_UTF8) :
                        {
                            U8 offs = 0;
                            if (tag.datalen > 3 && ((unsigned char*)tag.data)[0] == 0xEF && ((unsigned char*)tag.data)[1] == 0xBB && ((unsigned char*)tag.data)[2] == 0xBF)
                                offs = 3;
                            std::string out((char*)tag.data + offs, tag.datalen - offs);
                            if (out.length() && out[out.size() - 1] == 0)
                                out.erase(out.size() - 1);
                            mMetadata[name] = out;
                            LL_INFOS("FMOD") << tag.name << "(UTF8): " << out << LL_ENDL;
                            break;
                        }
                        case(FMOD_TAGDATATYPE_STRING_UTF16):
                        {
                            std::string out = utf16input_to_utf8((unsigned char*)tag.data, tag.datalen, UTF16);
                            if (out.length() && out[out.size() - 1] == 0)
                                out.erase(out.size() - 1);
                            mMetadata[name] = out;
                            LL_INFOS("FMOD") << tag.name << "(UTF16): " << out << LL_ENDL;
                            break;
                        }
                        case(FMOD_TAGDATATYPE_STRING_UTF16BE):
                        {
                            std::string out = utf16input_to_utf8((unsigned char*)tag.data, tag.datalen, UTF16BE);
                            if (out.length() && out[out.size() - 1] == 0)
                                out.erase(out.size() - 1);
                            mMetadata[name] = out;
                            LL_INFOS("FMOD") << tag.name << "(UTF16BE): " << out << LL_ENDL;
                            break;
                        }
                        default:
                            break;
                    }
                }

                mMetadataUpdateSignal(mMetadata);
            }

            if (starving)
            {
                bool paused = false;
                if (!Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->getPaused(&paused), "FMOD:Channel::getPaused") && !paused)
                {
                    LL_INFOS("FMOD") << "Stream starvation detected! Pausing stream until buffer nearly full." << LL_ENDL;
                    LL_INFOS("FMOD") << "  (diskbusy=" << diskbusy << ")" << LL_ENDL;
                    LL_INFOS("FMOD") << "  (progress=" << progress << ")" << LL_ENDL;
                    Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPaused(true), "FMOD::Channel::setPaused");
                }
            }
            else if (progress > 80)
            {
                Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPaused(false), "FMOD::Channel::setPaused");
            }
        }
    }
}

void LLStreamingAudio_FMODSTUDIO::stop()
{

    mMetadata = LLSD::emptyMap();
    mMetadataUpdateSignal(mMetadata);

    if (mFMODInternetStreamChannelp)
    {
        Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPaused(true), "FMOD::Channel::setPaused");
        Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPriority(0), "FMOD::Channel::setPriority");
        if (mStreamDSP)
        {
            Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->removeDSP(mStreamDSP), "FMOD::Channel::removeDSP");
        }
        mFMODInternetStreamChannelp = nullptr;
    }

    if (mCurrentInternetStreamp)
    {
        LL_INFOS("FMOD") << "Stopping internet stream: " << mCurrentInternetStreamp->getURL() << LL_ENDL;
        if (mCurrentInternetStreamp->stopStream())
        {
            delete mCurrentInternetStreamp;
        }
        else
        {
            LL_WARNS("FMOD") << "Pushing stream to dead list: " << mCurrentInternetStreamp->getURL() << LL_ENDL;
            mDeadStreams.push_back(mCurrentInternetStreamp);
        }
        mCurrentInternetStreamp = nullptr;
    }
}

void LLStreamingAudio_FMODSTUDIO::pause(int pauseopt)
{
    if (pauseopt < 0)
    {
        pauseopt = mCurrentInternetStreamp ? 1 : 0;
    }

    if (pauseopt)
    {
        if (mCurrentInternetStreamp)
        {
            LL_INFOS("FMOD") << "Pausing internet stream" << LL_ENDL;
            stop();
        }
    }
    else
    {
        start(getURL());
    }
}


// A stream is "playing" if it has been requested to start.  That
// doesn't necessarily mean audio is coming out of the speakers.
int LLStreamingAudio_FMODSTUDIO::isPlaying()
{
    if (mCurrentInternetStreamp)
    {
        return 1; // Active and playing
    }
    else if (!mURL.empty())
    {
        return 2; // "Paused"
    }
    else
    {
        return 0;
    }
}


F32 LLStreamingAudio_FMODSTUDIO::getGain()
{
    return mGain;
}


std::string LLStreamingAudio_FMODSTUDIO::getURL()
{
    return mURL;
}


void LLStreamingAudio_FMODSTUDIO::setGain(F32 vol)
{
    mGain = vol;

    if (mFMODInternetStreamChannelp)
    {
        vol = llclamp(vol * vol, 0.f, 1.f); //should vol be squared here?

        Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setVolume(vol), "FMOD::Channel::setVolume");
    }
}

/* virtual */
bool LLStreamingAudio_FMODSTUDIO::getWaveData(float* arr, S32 count, S32 stride/*=1*/)
{
    if (count > (WAVE_BUFFER_SIZE / 2))
        LL_ERRS("AudioImpl") << "Count=" << count << " exceeds WAVE_BUFFER_SIZE/2=" << WAVE_BUFFER_SIZE << LL_ENDL;

    if(!mFMODInternetStreamChannelp || !mCurrentInternetStreamp)
        return false;

    bool muted = false;
    FMOD_RESULT res = mFMODInternetStreamChannelp->getMute(&muted);
    if(res != FMOD_OK || muted)
        return false;
    {
        U32 buff_size;
        {
            LLMutexLock lock(&gWaveDataMutex);
            gWaveBufferMinSize = count;
            buff_size = gWaveDataBufferSize;
            if (!buff_size)
                return false;
            memcpy(arr, gWaveDataBuffer + WAVE_BUFFER_SIZE - buff_size, llmin(U32(count), buff_size) * sizeof(float));
        }
        if (buff_size < U32(count))
            memset(arr + buff_size, 0, (count - buff_size) * sizeof(float));
    }
    return true;
}

///////////////////////////////////////////////////////
// manager of possibly-multiple internet audio streams

LLAudioStreamManagerFMODSTUDIO::LLAudioStreamManagerFMODSTUDIO(FMOD::System *system, FMOD::ChannelGroup *group, const std::string& url) :
mSystem(system),
mChannelGroup(group),
mStreamChannel(nullptr),
mInternetStream(nullptr),
mReady(false),
mInternetStreamURL(url)
{
    FMOD_RESULT result = mSystem->createStream(url.c_str(), FMOD_2D | FMOD_NONBLOCKING | FMOD_IGNORETAGS, nullptr, &mInternetStream);

    if (result != FMOD_OK)
    {
        LL_WARNS("FMOD") << "Couldn't open fmod stream, error "
            << FMOD_ErrorString(result)
            << LL_ENDL;
        mReady = false;
        return;
    }

    mReady = true;
}

FMOD::Channel *LLAudioStreamManagerFMODSTUDIO::startStream()
{
    // We need a live and opened stream before we try and play it.
    if (!mInternetStream || getOpenState() != FMOD_OPENSTATE_READY)
    {
        LL_WARNS("FMOD") << "No internet stream to start playing!" << LL_ENDL;
        return nullptr;
    }

    if (mStreamChannel)
        return mStreamChannel;  //Already have a channel for this stream.

    Check_FMOD_Stream_Error(mSystem->playSound(mInternetStream, mChannelGroup, true, &mStreamChannel), "FMOD::System::playSound");
    return mStreamChannel;
}

bool LLAudioStreamManagerFMODSTUDIO::stopStream()
{
    if (mInternetStream)
    {


        bool close = true;
        switch (getOpenState())
        {
        case FMOD_OPENSTATE_CONNECTING:
            close = false;
            break;
        default:
            close = true;
        }

        if (close)
        {
            mInternetStream->release();
            mStreamChannel = NULL;
            mInternetStream = NULL;
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

FMOD_OPENSTATE LLAudioStreamManagerFMODSTUDIO::getOpenState(unsigned int* percentbuffered, bool* starving, bool* diskbusy)
{
    FMOD_OPENSTATE state;
    FMOD_RESULT result = mInternetStream->getOpenState(&state, percentbuffered, starving, diskbusy);
    if (result != FMOD_OK)
    {
        LL_WARNS("FMOD") << FMOD_ErrorString(result) << LL_ENDL;
    }
    return state;
}

void LLStreamingAudio_FMODSTUDIO::setBufferSizes(U32 streambuffertime, U32 decodebuffertime)
{
    Check_FMOD_Stream_Error(mSystem->setStreamBufferSize(streambuffertime / 1000 * 128 * 128, FMOD_TIMEUNIT_RAWBYTES), "FMOD::System::setStreamBufferSize");
    FMOD_ADVANCEDSETTINGS settings = { };
    settings.cbSize = sizeof(settings);
    settings.defaultDecodeBufferSize = decodebuffertime;//ms
    Check_FMOD_Stream_Error(mSystem->setAdvancedSettings(&settings), "FMOD::System::setAdvancedSettings");
}

void LLStreamingAudio_FMODSTUDIO::cleanupWaveData()
{
    if (mStreamGroup)
    {
        Check_FMOD_Stream_Error(mStreamGroup->release(), "FMOD::ChannelGroup::release");
        mStreamGroup = nullptr;
    }

    if(mStreamDSP)
        Check_FMOD_Stream_Error(mStreamDSP->release(), "FMOD::DSP::release");
    mStreamDSP = nullptr;
}
