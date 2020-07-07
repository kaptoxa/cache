#pragma once

#include "cache_structs.h"
#include "icache.h"

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>


namespace peer_app
{

namespace cache
{

class cache;

class cache_writer:
	public iwriter
{
	cache* _cache;
	content_id_t _cid;

	boost::scoped_ptr<boost::interprocess::file_mapping> frames_fm;
	boost::scoped_ptr<boost::interprocess::file_mapping> timestamps_fm;

	uint64_t total_frames_size;
	uint64_t frames_size;
	uint64_t timestamps_size;

	uint64_t _timestamps[8];
	boost::shared_ptr<cache_content_t> diapasons;



	void init_details( const uint8_t, const codec_details*);
	void init_timestamps( uint64_t );

	void write_frames(frame_data*, uint32_t count, bool is_key_frame);
	void write_timestamps(frame_data*, uint32_t count, bool is_key_frame);

	void recalc_ranges( uint64_t ts, uint64_t duration );

public:

	cache_writer( cache*, const content_id_t&, const codec_details*,
		uint8_t, uint64_t, const uint64_t*);
	~cache_writer();

	virtual void pass_frames(frame_data*, uint32_t count, bool is_key_frame);
	virtual void restart(const uint64_t* timestamps){};
	virtual void release();


};

}

}

