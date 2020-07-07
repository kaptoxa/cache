#include "stdafx.h"

#include "codec_details.h"
#include "cache.h"
#include "cache_writer.h"
#include "lock.h"

#include <exception.h>
#include <pthread_stuff.h>
#include <cid_ops.h>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"

#include <fstream>








#if defined(_DEBUG) && defined(_WIN32)
#define new DEBUG_NEW
#endif



using namespace boost::interprocess;
namespace fs = boost::filesystem;

namespace peer_app
{

namespace cache
{

const uint64_t add_size = 1048576;
uint64_t file_size(const char* name);


bool enlarge_file( const char* filename, uint64_t new_size, bool to_create = false )
{
	std::ofstream file(filename, 
		to_create ? (std::ios::binary | std::ios::out) :
		(std::ios::binary | std::ios::in | std::ios::out));
     if(!file){
        return false;
     }
     if(!file.seekp(static_cast<std::streamoff>(new_size - 1))){
        return false;
     }
     if(!file.write("", 1)){
        return false;
     }
	 return true;
}



cache_writer::cache_writer(
	cache *pcache, const content_id_t &cid,
	const codec_details *details,
	uint8_t stream_count,
	uint64_t total_duration,
	const uint64_t *timestamps):

	_cache(pcache),_cid(cid)
{
	try
	{
		*_cache->log << " create new writer... " << cid_to_string(cid) << "\n";
		_cache->log->flush();

		{
			mutex_locker ml( _cache->compass_mutex );
			compass_t::iterator it=_cache->compass->find(cid);
			if (it==pcache->compass->end())
			{
				boost::shared_ptr<cache_content_t> cc( new cache_content_t( _cache->pthreads ) );
				pcache->compass->insert( std::make_pair(cid, cc) );
			}
			if ((it=_cache->compass->find(cid))!=_cache->compass->end())
				diapasons = it->second;
		}

		{
			mutex_locker( *diapasons->counter_mutex );
			*diapasons->counter += 1;
		}

		{
			mutex_locker( *diapasons->enlarge_mutex );
			frames_size = *diapasons->file_size;
		}

		init_details(stream_count,details);
		init_timestamps(total_duration);

	}
	catch (std::exception& ex)
	{
		*_cache->log << " <<writer::writee>>" << ex.what() << "\n";
		_cache->log->flush();
		throw exception(std::string(ex.what()));
	}
}

cache_writer::~cache_writer()
{
}

void cache_writer::pass_frames(frame_data* fd, uint32_t count, bool is_key_frame)
{
	if( count )
	{
		try
		{
			uint8_t index = 0; // index of main frame(video frame)
			shifts_t::iterator it = diapasons->shifts->find(fd[index].timestamp);
			if( diapasons->shifts->end()!=it )
				return;

			write_timestamps(fd,count,is_key_frame);
			write_frames(fd,count,is_key_frame);
		}
		catch (std::exception& ex) {
			*_cache->log << " <<writer::pass_frame>>" << ex.what() << "\n";
			_cache->log->flush();
			throw exception(std::string(ex.what()));
		}
	}
}

void cache_writer::release()
{
	mutex_locker( *diapasons->counter_mutex );
	*diapasons->counter -= 1;

	assert( *diapasons->counter>=0 );
	delete this;
}




void cache_writer::init_details(const uint8_t stream_count, const codec_details* details)
{
	int file_size = sizeof(uint32_t) + sizeof(uint8_t) + stream_count * (sizeof(base_codec_details) + sizeof(uint32_t));
	for(int i = 0; i <stream_count; i++)
		file_size += details[i].extra_data_size;

	std::string file_name = _cache->cache_path + cid_to_string(_cid) + ".frames";
	fs::path p( file_name );

	bool to_create = false;
	frames_size = total_frames_size = 0;
	if ( fs::exists( p ) && !fs::is_directory( p ) )
		frames_size = total_frames_size = fs::file_size(p);
	else
	{
		mutex_locker( *diapasons->enlarge_mutex );
		if( frames_size<file_size )
		{
			to_create = true;
			enlarge_file( file_name.c_str(), file_size, true );
			frames_size = *diapasons->file_size = file_size;
		}
	}

	frames_fm.reset( new file_mapping(file_name.c_str(), read_write) );
	if( to_create )
	{
		mapped_region region( *frames_fm, read_write, 0, file_size  );
		char *addr       = (char*) region.get_address();

		std::memcpy(addr, &cache_version, sizeof(uint32_t) ); addr += sizeof(uint32_t);
		std::memcpy(addr, &stream_count, sizeof(uint8_t) ); addr += sizeof(uint8_t);
		for(int i = 0; i <stream_count; i++)
		{
			std::memcpy(addr, &details[i], sizeof(base_codec_details) ); addr += sizeof(base_codec_details);
			std::memcpy(addr, &details[i].extra_data_size, sizeof(uint32_t) ); addr += sizeof(uint32_t);
			if (details[i].extra_data.size())
			{
				std::memcpy(addr, &details[i].extra_data.front() , details[i].extra_data_size ); addr += details[i].extra_data_size ;
			}
		}
	}
}

void cache_writer::init_timestamps(uint64_t total_duration)
{
	int file_size = sizeof(uint64_t) + sizeof(uint32_t);

	std::string file_name = _cache->cache_path + cid_to_string(_cid) + ".timestamps";
	fs::path p( file_name );

	bool to_create = false;
	timestamps_size = 0;
	if ( fs::exists( p ) && !fs::is_directory( p ) )
		timestamps_size = fs::file_size(p);
	else
		enlarge_file( file_name.c_str(), file_size, true );

	timestamps_fm.reset( new file_mapping(file_name.c_str(), read_write) );
	if( timestamps_size<file_size )
	{
		mapped_region region( *timestamps_fm, read_write, 0, file_size  );
		char *addr       = (char*) region.get_address();

		std::memcpy(addr, &cache_version, sizeof(uint32_t) ); addr += sizeof(uint32_t);
		std::memcpy(addr, &total_duration, sizeof(uint64_t) ); addr += sizeof(uint64_t);
		timestamps_size = file_size;
	}
}





void cache_writer::write_frames(frame_data* fd, uint32_t count, bool is_key_frame)
{
	uint32_t size = 2 * sizeof(uint32_t) + count * (sizeof(frame_data) + sizeof(uint32_t));
	for(uint32_t i=0; i<count; i++)
		size +=	fd[i].len ;

	std::string file_name = _cache->cache_path + cid_to_string( _cid ) + ".frames";
	if( total_frames_size<(frames_size + size))
	{
		mutex_locker ml( *diapasons->enlarge_mutex);
		if( !_cache->check_space( _cid ) )
			throw exception("i can't write to cache! not enough disk space");

		total_frames_size = frames_size + add_size;
		enlarge_file( file_name.c_str(), total_frames_size );
		*(diapasons->file_size) = total_frames_size;
	}

	mapped_region region( *frames_fm, read_write, frames_size, size );
	char * addr       = (char*) region.get_address(); 

	std::memcpy(addr, &count, sizeof(uint32_t)); addr += sizeof(uint32_t);
	std::memcpy(addr, &size, sizeof(uint32_t));	addr += sizeof(uint32_t);

	uint32_t value = 0;
	for(uint32_t i=0; i<count; i++)
	{
		std::memcpy(addr, &fd[i], sizeof(frame_data)); addr += sizeof(frame_data);
		std::memcpy(addr, fd[i].pdata, fd[i].len); addr += fd[i].len;

		value = crc( (char*)fd[i].pdata, fd[i].len );
		std::memcpy(addr, &value, sizeof(uint32_t) ); addr += sizeof(uint32_t);
	}

	frames_size += size;

	recalc_ranges(fd[0].timestamp,fd[0].duration);
}

void cache_writer::write_timestamps(frame_data* fd, uint32_t count, bool is_key_frame)
{
	uint8_t index = 0; // index of main frame(video frame)
	uint32_t size = 3 * sizeof(uint64_t); // size of one record

	std::string path = _cache->cache_path + cid_to_string( _cid ) + ".timestamps";
	enlarge_file( path.c_str(), timestamps_size + size, !timestamps_size );

	mapped_region region( *timestamps_fm, read_write, timestamps_size, size );
	char * addr       = (char*) region.get_address(); 

	std::memcpy(addr, &fd[index].timestamp , sizeof(fd[index].timestamp));
	addr += sizeof(fd[index].timestamp);
	std::memcpy(addr, &fd[index].duration , sizeof(fd[index].duration));
	addr += sizeof(fd[index].duration);

	uint64_t offset = is_key_frame ? frames_size | hkf : frames_size ; 
	std::memcpy(addr, &offset , sizeof(offset));
	addr += sizeof(offset);

	frame_node fn;
	fn.duration = fd[index].duration;
	fn.offset = offset;
	diapasons->shifts->insert( std::make_pair(fd[index].timestamp,fn) );

	timestamps_size += size;
}


void cache_writer::recalc_ranges( uint64_t ts, uint64_t duration )
{
	mutex_locker ml( *diapasons->ranges_mutex );
	boost::shared_ptr<content_state_t> cr = diapasons->cont_ranges;
	ts_range_t new_range={ts,ts+duration};
		
	if( !cr->ranges.size() )
		cr->ranges.push_back( new_range );
	else if( 1==cr->ranges.size() )
	{
		if( cr->ranges.begin()->ts_end==ts )
			cr->ranges.begin()->ts_end += duration;
		else
		{
			ts_range_t new_range={ts,ts};
			ts_ranges_t::iterator rit =	std::upper_bound( cr->ranges.begin(),cr->ranges.end(), ts);
			cr->ranges.insert( rit, new_range );
		}
	}
	else
	{
		ts_ranges_t::iterator rit = std::upper_bound( cr->ranges.begin(),cr->ranges.end(), ts);
		if( --rit!=cr->ranges.end() && rit->ts_end==ts )
			rit->ts_end += duration;
	}
}



}

}

