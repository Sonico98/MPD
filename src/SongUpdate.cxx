/*
 * Copyright (C) 2003-2014 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h" /* must be first for large file support */
#include "Song.hxx"
#include "DetachedSong.hxx"
#include "util/UriUtil.hxx"
#include "Directory.hxx"
#include "Mapper.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/Traits.hxx"
#include "fs/FileSystem.hxx"
#include "decoder/DecoderList.hxx"
#include "tag/Tag.hxx"
#include "tag/TagBuilder.hxx"
#include "tag/TagHandler.hxx"
#include "tag/TagId3.hxx"
#include "tag/ApeTag.hxx"
#include "TagFile.hxx"
#include "TagStream.hxx"

#include <assert.h>
#include <string.h>
#include <sys/stat.h>

Song *
Song::LoadFile(const char *path_utf8, Directory &parent)
{
	Song *song;
	bool ret;

	assert(!uri_has_scheme(path_utf8));
	assert(strchr(path_utf8, '\n') == nullptr);

	song = NewFile(path_utf8, parent);

	//in archive ?
	if (parent.device == DEVICE_INARCHIVE) {
		ret = song->UpdateFileInArchive();
	} else {
		ret = song->UpdateFile();
	}
	if (!ret) {
		song->Free();
		return nullptr;
	}

	return song;
}

/**
 * Attempts to load APE or ID3 tags from the specified file.
 */
static bool
tag_scan_fallback(Path path,
		  const struct tag_handler *handler, void *handler_ctx)
{
	return tag_ape_scan2(path, handler, handler_ctx) ||
		tag_id3_scan(path, handler, handler_ctx);
}

bool
Song::UpdateFile()
{
	const auto path_fs = map_song_fs(*this);
	if (path_fs.IsNull())
		return false;

	struct stat st;
	if (!StatFile(path_fs, st) || !S_ISREG(st.st_mode))
		return false;

	TagBuilder tag_builder;
	if (!tag_file_scan(path_fs,
			   full_tag_handler, &tag_builder))
		return false;

	if (tag_builder.IsEmpty())
		tag_scan_fallback(path_fs, &full_tag_handler,
				  &tag_builder);

	mtime = st.st_mtime;

	tag_builder.Commit(tag);
	return true;
}

bool
Song::UpdateFileInArchive()
{
	/* check if there's a suffix and a plugin */

	const char *suffix = uri_get_suffix(uri);
	if (suffix == nullptr)
		return false;

	if (!decoder_plugins_supports_suffix(suffix))
		return false;

	const auto path_fs = map_song_fs(*this);
	if (path_fs.IsNull())
		return false;

	TagBuilder tag_builder;
	if (!tag_stream_scan(path_fs.c_str(), full_tag_handler, &tag_builder))
		return false;

	tag_builder.Commit(tag);
	return true;
}

bool
DetachedSong::Update()
{
	if (IsAbsoluteFile()) {
		const AllocatedPath path_fs =
			AllocatedPath::FromUTF8(GetRealURI());

		struct stat st;
		if (!StatFile(path_fs, st) || !S_ISREG(st.st_mode))
			return false;

		TagBuilder tag_builder;
		if (!tag_file_scan(path_fs, full_tag_handler, &tag_builder))
			return false;

		if (tag_builder.IsEmpty())
			tag_scan_fallback(path_fs, &full_tag_handler,
					  &tag_builder);

		mtime = st.st_mtime;
		tag_builder.Commit(tag);
		return true;
	} else if (IsRemote()) {
		TagBuilder tag_builder;
		if (!tag_stream_scan(uri.c_str(), full_tag_handler,
				     &tag_builder))
			return false;

		mtime = 0;
		tag_builder.Commit(tag);
		return true;
	} else
		// TODO: implement
		return false;
}
