#pragma once

#include "cache_structs.h"
#include "codec_details.h"
#include "icache.h"
#include "buf_t.h"

namespace peer_app
{

namespace cache
{

class cache_frame;
class lock;
class cache;


class cache_frame:
	public iframe
{
public:
	buf_t* buf;

	cache_frame()
	{
		buf=0;
	}
	~cache_frame()
	{
		if (buf)
			buf->release("cache_frame::release");
	}

	virtual buf_t* get_buf()
	{
		return buf;
	}
	virtual void release() const {
		delete this;
	}
};


class lock:
	public ilock,
	public refcounted_obj
{
	cache* _cache;
	content_id_t _cid;

	boost::shared_ptr<cache_content_t> diapasons;
	//boost::scoped_ptr<shifts_t> shifts;
	
	uint64_t _ts;			// current timestamp
	ts_range_t _range;

	uint8_t _streams_count;
	codec_details _details[8];


	void init_details();
	void init_timestamps();

	bool find_key_frame(ts_range_t* key_range);

public:

	lock(cache* _cache, const content_id_t&,
		  const uint8_t* streams, uint8_t streams_count, //absolute stream numbers
		  const ts_range_t&); //range to lock
	~lock();


	virtual void begin_reading(
			uint64_t* start_timestamp,
			bool to_navigate_to_keyframe);

	//read one frame of a main stream (first position in a vector) and a number of secondary ones.
	//returns false to indicate the end of stream or lock.
	//begin_reading should be called first.
	virtual bool read(
			std::vector<iframe*>& frames,
			bool& keyframe);

	//fill the details
	virtual void get_stream_details(
		codec_details* det);

	//change the lock to new range
	virtual bool change(
		const ts_range_t& range);

	//get current locked range
	virtual ts_range_t get_range();

	virtual void release();
};

}

}

