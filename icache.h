#pragma once

#include <common_structs.h>

struct codec_details;

namespace peer_app {

class application;

namespace cache {

class media_files_reader;

struct frame_data
{
	const void* pdata; //pointer to frame data
	uint32_t len; //length of frame data
	uint64_t timestamp; //timestamp of the frame (in 100 nanosec)
	uint64_t duration; //duration of the frame (in 100 nanosec)
	uint8_t stream_no; //relative stream number
};

/*
iwriter is used to dump all the data that peer gets to the cache.
there may be multiple instances of icontent_writers that act simultaneously
and asynchronously with readers/lockers. it's cache implementation' responsibility to
determine what content to store in the cache and what to bypass.
*/

struct iwriter
{
	virtual void pass_frames(frame_data*, uint32_t count, bool is_key_frame) = 0; //write frames portion to the cache
		//this frame portion includes next main stream frame and all the secondary stream frames that
		//belong to this main stream frame
	virtual void restart(const uint64_t* timestamps) = 0; //called when seeking performed
	virtual void release() = 0;
};

/*
iframe describes frame internals
*/

struct iframe : frame_data
{
	virtual void release() const = 0;
};

/*
ilock interface is used to lock some range of content to read. until the lock is released
content is ok to read. there may be only one icontent_reader per lock. locks can be extended
or shrinked via change() call.

sequential reading of the frames is ordered by time
and related to main stream timestamp - duration. so, first frame of the main stream is read, then
all the frames of other streams are read (frame 0' timestamp <= timestamps < frame 1' timestamp) and only then
the next frame of the main stream is read

access to ilock interface is not thread-safe (no simultaneous func calls)
*/

struct ilock
{
	//can be called multiple times. in that case old reading should be discarded
	virtual void begin_reading(
			uint64_t* start_timestamp,
			bool to_navigate_to_keyframe) = 0;

	//read one frame of a main stream (first position in a vector) and a number of secondary ones.
	//returns false to indicate the end of stream or lock.
	//begin_reading should be called first.
	virtual bool read(
			std::vector<iframe*>& frames,
			bool& keyframe) = 0;

	//fill the details
	virtual void get_stream_details(
			codec_details* det) = 0;

	//change the lock to new range
	virtual bool change(
			const ts_range_t& range) = 0;

	//get current locked range
	virtual ts_range_t get_range() = 0;

	virtual void release() = 0;
};

/*
icache interface is the root interface for cache management.

access to icache interface is thread-safe (simultaneous func calls)
*/

struct icache
{
	virtual ilock* lock(
			const content_id_t&, //content to lock
			const uint8_t* streams, uint8_t streams_count, //absolute stream numbers
			const ts_range_t&) = 0; //range to lock

	virtual iwriter* write(
			const content_id_t&, //content to write
			const uint8_t* streams,uint8_t streams_count, //absolute stream numbers
			const codec_details*, //array of codec_details,
			uint64_t total_duration, //
			const uint64_t* timestamps //array of timestamps per stream
			) = 0;

	virtual void list(cache_state_t&) = 0; //list cached contents

	virtual void get_diff(cache_diff_t&) = 0;

	virtual void commit() = 0;

	virtual void delete_content(const content_id_t&) = 0;

	virtual void add_content(const content_id_t&, const std::string& path, const public_key_t& public_key) = 0;

	virtual media_files_reader* get_media_files_reader() = 0;

	virtual void release() = 0;
};

icache* make_icache(application*); //create icache instance

} }
