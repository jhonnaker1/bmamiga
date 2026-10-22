/*
 * zz9000_media.cpp - the ZZ9000 card's media player for Amiberry on
 * Circle, in place of src/zz9000_sdk_media_av.cpp.
 *
 * Amiberry's decodes on a thread of its own (std::thread, which Circle
 * hasn't) with FFmpeg, which BMAmiga doesn't have either. Without FFmpeg
 * Amiberry's own fails every stream; so does this one, at once.
 *
 * Compiled with Amiberry's flags (src/port/amiberry.mk, BMA_EXTRA).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include "sysconfig.h"
#include "sysdeps.h"
#include "zz9000_sdk_media_av.h"

namespace zz9000_sdk {

struct MediaAv::State {
};

MediaAv::MediaAv(uint32_t video_codec, uint32_t container, uint32_t audio_codec)
	: video_codec_(video_codec), container_(container), audio_codec_(audio_codec)
{
	failed_ = true;
	done_ = true;
}

MediaAv::~MediaAv()
{
}

uint32_t MediaAv::append(const uint8_t *data, uint32_t length)
{
	(void) data;
	return length;
}

void MediaAv::finish_input()
{
	eof_ = true;
}

bool MediaAv::header_ready() const
{
	return false;
}

bool MediaAv::failed() const
{
	return true;
}

bool MediaAv::worker_waiting() const
{
	return false;
}

bool MediaAv::pop_audio(MediaAvAudio &audio)
{
	(void) audio;
	return false;
}

bool MediaAv::has_audio() const
{
	return false;
}

int MediaAv::decode(MediaAvFrame &frame, uint32_t flags)
{
	(void) frame;
	(void) flags;
	return -1;
}

} // namespace zz9000_sdk
