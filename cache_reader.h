#pragma once

#include "codec_details.h"
#include "icache.h"
#include "buf_t.h"


#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

using namespace boost::interprocess;


namespace peer_app
{

class cache_frame;
class lock;


class cache_frame:
	public iframe
{
public:
	uint64_t duration,_timestamp; //in 100 nanoseconds
	uint8_t _stream_no;
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
	virtual uint8_t stream_no()
	{
		return _stream_no;
	}
	virtual uint64_t timestamp()
	{
		return _timestamp;
	}

	virtual void release() {
		delete this;
	}
};


class lock:
	public ilock,
	public icontent_reader,
	public refcounted_obj
{
	// reader
	uint8_t _streams_count;
	std::vector<uint64_t> _streams_positions;

	uint32_t _frame_no;
	uint64_t* _timestamps;


	boost::scoped_ptr<file_mapping>		content_file;
	boost::scoped_ptr<mapped_region>	_offsets;
	//reader


//	cache* _cache;

	range_t range;

	codec_details _details[8];

	std::string file_name;

public:

	lock(const content_id_t&, uint8_t, uint32_t, uint32_t);
	~lock();

//	dummy_content_reader*	_cache_reader; 

// ilock virtuals:
	virtual icontent_reader* read(uint32_t* frame_no, //first frame returned by get_next_frame()
		uint64_t* timestamps, bool to_navigate_to_keyframe); //shifts - array in ms
	virtual iframe* get_next_frame(){return 0;};

	virtual bool get_next_frames(std::vector<iframe*>&);
	virtual void release();
	virtual void get_stream_details(codec_details* det);

	virtual bool change(range_t);
	virtual range_t get_range();
};

/*
class dummy_content_reader:
	public icontent_reader
{


public:

	dummy_content_reader( const char* file_name,
		uint32_t frame_start, uint32_t frame_end);
		//uint32_t* frame_no, //first frame returned by get_next_frame()
		//uint64_t* timestamps );
	~dummy_content_reader();

	virtual bool change(range_t new_range);
	virtual range_t get_range();

	virtual iframe* get_next_frame(){return 0;};
	virtual void release();
};
*/

}
