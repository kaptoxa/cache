#include "stdafx.h"
#include "codec_details.h"
#include "lock.h"
#include "cache.h"
#include "application.h"

#include <pthread_stuff.h>
#include <cid_ops.h>
#include <exception.h>

#include <fstream>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"



#if defined(_DEBUG) && defined(_WIN32)
#define new DEBUG_NEW
#endif


using namespace boost::interprocess;
namespace fs = boost::filesystem;

namespace peer_app
{

namespace cache
{

uint64_t file_size(const char* name);
boost::scoped_ptr<std::ofstream> lock_log( new std::ofstream("lock.log"));


lock::lock(cache* pcache, const content_id_t& cid, 
		  const uint8_t* streams, uint8_t streams_count, //absolute stream numbers
		  const ts_range_t& range) //range to lock   //uint8_t streams_count, uint32_t frame_start, uint32_t frame_end)
		  :_cache(pcache),_cid(cid),_range(range)
{
	try
	{
		{
			mutex_locker ml( _cache->compass_mutex );
			compass_t::iterator it=_cache->compass->find(cid);
			if ((it=_cache->compass->find(cid))!=_cache->compass->end())
				diapasons = it->second;
			else
				throw exception("i'am sorry, this lock has NOT been created. i cant' lock this range");
		}

		init_timestamps();
		init_details();


		ts_range_t r=range;
		if( !change(r) )
			throw exception("i'am sorry, this lock has NOT been created. i cant' lock this range");

		{
			mutex_locker counter_l( *diapasons->counter_mutex );
			*diapasons->counter += 1;
		}

		*lock_log << "congratulations! lock has been created.\n";
		lock_log->flush();
	}
	catch (std::exception& ex)
	{
		*lock_log << " <<lock::lock>>" << ex.what() << "\n";
		lock_log->flush();
		throw exception(std::string(ex.what()));
	}
	addref("lock::lock");
}

lock::~lock()
{
}

void lock::release()
{
	if( refcounted_obj::release("lock::release")!=0 )
	{
		mutex_locker  ml( *diapasons->counter_mutex );
		*diapasons->counter -= 1;

		assert( *diapasons->counter>=0 );
	}
}

bool lock::find_key_frame(ts_range_t* key_range)
{
	ts_range_t kr = *key_range;

	mutex_locker ml( *diapasons->ranges_mutex );
	boost::shared_ptr<content_state_t> cr = diapasons->cont_ranges;

	if( !cr )
		return false;

	if( cr->ranges.size() )
	{
		ts_ranges_t::iterator rit =
			std::upper_bound(cr->ranges.begin(),cr->ranges.end(),kr.ts_start);

		if( cr->ranges.end()==rit )
			return false;


		kr.ts_end = rit->ts_end;
		boost::shared_ptr<shifts_t> shifts = diapasons->shifts;
		shifts_t::iterator it = shifts->upper_bound(kr.ts_start);

		if( shifts->end()!=it )
		{
			while( it->first>=rit->ts_start && !it->second.is_key_frame() )	it--;
			if( it->first>=rit->ts_start && it->second.is_key_frame() )
			{
				kr.ts_start = it->first;
				*key_range = kr;
				return true;
			}

			it = shifts->upper_bound(kr.ts_start);

			while( it->first<=rit->ts_end && !it->second.is_key_frame() )	it++;
			if( it->first<=rit->ts_start && it->second.is_key_frame() )
			{
				kr.ts_start = it->first;
				*key_range = kr;
				return true;
			}
		}
	}

	return false;
}

bool lock::change(const ts_range_t& new_range)
{
	bool acceptive = false;
	try
	{
		_range.ts_start = new_range.ts_start;
		acceptive = find_key_frame(&_range)
			&& !(_range.ts_start>new_range.ts_end)
			&& !(_range.ts_end<new_range.ts_start);

		return acceptive;
	}
	catch (std::exception& ex)
	{
		*lock_log << " <<lock::change>>" << ex.what() << "\n";
		lock_log->flush();
		throw exception(std::string(ex.what()));
	}
}

ts_range_t lock::get_range()
{
	return _range;
}

void lock::begin_reading(
			uint64_t* start_timestamp,
			bool to_navigate_to_keyframe)
{
	try
	{
		_ts = *start_timestamp;
		if( to_navigate_to_keyframe )
		{
			ts_range_t range = {_ts,_ts};
			if( !find_key_frame(&range) )
				assert(0); // change and find_key_frame don't work.
			_ts = range.ts_start;
		}
		else
		{
			shifts_t::iterator it = diapasons->shifts->lower_bound(_ts);
			if( diapasons->shifts->end()!=it )
				_ts = it->first;
		}
		*start_timestamp = _ts;
	}
	catch(std::exception& ex)
	{
		*lock_log << "<<lock::begin_reading>>" << ex.what() << "\n";
		lock_log->flush();
		throw exception(std::string(ex.what()));
	}
}

bool lock::read(std::vector<iframe*>& frames,bool& keyframe)
{
	keyframe=false; //todo
	try
	{
		shifts_t::iterator it = diapasons->shifts->find(_ts);
		if( diapasons->shifts->end()!=it )
		{
			keyframe = it->second.is_key_frame();
			uint64_t _offset = it->second.get_offset();


			std::string name = "cache\\" + cid_to_string(_cid) + ".frames";
			uint64_t frames_size = file_size(name.c_str());

			boost::scoped_ptr<file_mapping> frames_file( new file_mapping(name.c_str(), read_only) );
			boost::scoped_ptr<mapped_region> region( new mapped_region( *frames_file, read_only, _offset, 2 * sizeof(uint32_t)) );
			char* offset       = (char*) region->get_address();

			uint32_t size = 0;
			uint32_t count = 0;
			std::memcpy( &count, offset, sizeof(uint32_t)); offset += sizeof(uint32_t);
			std::memcpy( &size, offset, sizeof(uint32_t)); offset += sizeof(uint32_t);

			if( !(count && size) || count>255 )
			{
				//frames_file.reset(0);
				//erase_frame( current_frame_no );
				throw exception(" ACHTUNG! the frame has NOT been written completely.");
			}

			region.reset( new mapped_region( *frames_file, read_only, _offset, size ) );
			offset       = (char*) region->get_address() + 2 * sizeof(uint32_t);

			frames.clear();

			uint64_t delta_to_next_ts = 0;
			uint32_t cur_size = 0;
			uint32_t value = 0;
			frame_data fd;
			for(int i=0; i!=count; i++)
			{
				std::auto_ptr<cache_frame> frame(new cache_frame);

				if( (cur_size += sizeof(frame_data))>size )
					throw exception(" ACHTUNG! the frame has NOT been written completely.");

				std::memcpy( &fd, offset, sizeof(frame_data)); offset += sizeof(frame_data);

				frame->buf = buf_t::alloc( fd.len );
				frame->buf->addref("cache_frame::get_next_frame");
				frame->buf->size = fd.len ;

				if( (cur_size += fd.len)>size )
					throw exception(" ACHTUNG! the frame has NOT been written completely.");

				std::memcpy( frame->buf->data, offset, fd.len); offset += fd.len;

				if( (cur_size += sizeof(uint32_t))>size )
					throw exception(" ACHTUNG! the frame has NOT been written completely.");

				std::memcpy( &value, offset, sizeof(uint32_t) ); offset += sizeof(uint32_t);
				if(value!=crc( (char*)frame->buf->data, fd.len ))
				{
					//content_file.reset(0);
					//erase_frame( current_frame_no );
					throw exception(" ACHTUNG! the frame has NOT been written completely.");
				}

				frame->stream_no = fd.stream_no;
				frame->timestamp = fd.timestamp;
				frame->duration = fd.duration; 
				frame->len = fd.len;
				frame->pdata = frame->buf->data;

				if( !i )
					delta_to_next_ts = fd.duration; 

				frames.push_back( frame.release() );
			}

			_ts += delta_to_next_ts;

			return true;
		}

		return false;
	}
	catch(std::exception& ex)
	{
		*lock_log << "<<lock::get_next_frames>>" << ex.what() << "\n";
		lock_log->flush();
		throw exception(std::string(ex.what()));
	}
}

void lock::get_stream_details(codec_details* det)
{
	try
	{
		for(int i=0; i<_streams_count; i++)
		{
			std::memcpy( &det[i], &_details[i], sizeof(base_codec_details) );

			det[i].extra_data_size = _details[i].extra_data_size;
			det[i].extra_data.resize( det[i].extra_data_size );
			if (det[i].extra_data.size())
				std::memcpy( &det[i].extra_data.front() , &_details[i], det[i].extra_data_size );
		}
	}
	catch (std::exception& ex) {
		*lock_log << " <<lock::get_stream_details>>" << ex.what() << "\n";
		lock_log->flush();
		throw exception(std::string(ex.what()));
	}
}


void lock::init_details()
{
	std::string name = "cache\\" + cid_to_string(_cid) + ".frames";
	uint64_t frames_size = file_size(name.c_str());
	file_mapping frames_file( name.c_str(), read_only );

	mapped_region region( frames_file, read_only  );
	char* addr = (char*)region.get_address();

	uint64_t total_duration = 0;
	_streams_count = 0;
	uint64_t size = sizeof(uint8_t) + sizeof(uint32_t);

	uint32_t version = 0;
	std::memcpy( &version, addr, sizeof(uint32_t) ); addr += sizeof(uint32_t);
	if( version!=cache_version )
		throw exception("wrong cache version");


	std::memcpy( &_streams_count, addr, sizeof(uint8_t) ); addr += sizeof(uint8_t);
	for(int i=0; i<_streams_count; i++)
	{
		std::memcpy( &_details[i], addr, sizeof(base_codec_details) ); addr += sizeof(base_codec_details);
		std::memcpy( &_details[i].extra_data_size, addr, sizeof(uint32_t) ); addr += sizeof(uint32_t);

		_details[i].extra_data.resize( _details[i].extra_data_size );
		if (_details[i].extra_data.size())
		{
			std::memcpy( &_details[i].extra_data.front() , addr, _details[i].extra_data_size ); addr += _details[i].extra_data_size;
		}

		size += sizeof(base_codec_details) + sizeof(uint32_t) + _details[i].extra_data_size;
	}
}

void lock::init_timestamps()
{
}

}

}

