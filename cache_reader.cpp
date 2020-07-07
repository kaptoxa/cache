#include "stdafx.h"
#include "codec_details.h"
#include "lock.h"
#include <exception.h>


#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>


#if defined(_DEBUG) && defined(_WIN32)
#define new DEBUG_NEW
#endif

using namespace boost::interprocess;

namespace peer_app
{

const char* string_to_cid(const char* cid,content_id_t& out);
std::string cid_to_string(const content_id_t& cid);


lock::lock(const content_id_t& cid, uint8_t _streams_count, uint32_t frame_start, uint32_t frame_end)
//	_cache(_cache),
//	_streams_count(_streams_count)
{
	
//	_cache_reader = 0;
//	_cache_reader = new dummy_content_reader( file_name.c_str(), frame_start, frame_end);

	try
	{
		file_name = cid_to_string(cid);
		content_file.reset( new file_mapping(file_name.c_str(), read_only) );

		range.start		= frame_start;
		range.end		= frame_end;
	//	printf("  lock...");

		_frame_no = 0;
		//_frame_no = *frame_no;
		//_timestamps = timestamps;

		mapped_region region( *content_file, read_only  );
		char* addr       = (char*)region.get_address();


		uint8_t _streams_count = 0;
		uint64_t size = sizeof(uint8_t);

		std::memcpy( &_streams_count, addr, sizeof(uint8_t) );
		addr += sizeof(uint8_t);

		for(int i=0; i<_streams_count; i++)
		{
			std::memcpy( &_details[i], addr, sizeof(base_codec_details) );
			addr += sizeof(base_codec_details);

			std::memcpy( &_details[i].extra_data_size, addr, sizeof(uint32_t) );
			addr += sizeof(uint32_t);

			_details[i].extra_data.resize( _details[i].extra_data_size );
			std::memcpy( &_details[i].extra_data.front() , addr, _details[i].extra_data_size );
			addr += _details[i].extra_data_size;

			size += sizeof(base_codec_details) + sizeof(uint32_t) + _details[i].extra_data_size;
		}





		//uint32_t extra_size = 0;
		//uint8_t _stream_count = 0;
		//uint64_t size = sizeof(uint8_t);

		//std::memcpy( &_streams_count, addr, sizeof(uint8_t) );
		//addr += sizeof(uint8_t);

		//for(int i=0; i<_streams_count; i++)
		//{
		//	addr += sizeof(base_codec_details);
		//	std::memcpy( &extra_size, addr, sizeof(uint32_t) );
		//	addr += sizeof(uint32_t) + extra_size;

		//	size += sizeof(base_codec_details) + sizeof(uint32_t) + extra_size;
		//}



		uint32_t frames_count = 0;
		std::memcpy( &frames_count, addr, sizeof(uint32_t) );
		addr += sizeof(uint32_t);

		_offsets.reset( new mapped_region( *content_file, read_only, size,
						frames_count * sizeof(uint64_t) + sizeof(uint32_t) ) );

		range_t r={frame_start, frame_end};
		if( !change(r) )
			throw exception("i cant' lock this range\n");

		printf( " lock has been created![%d:%d]\n", range.start, range.end );

		_streams_positions.resize( _streams_count );
		for(int i=0; i<_streams_count; i++)
			_streams_positions[i] = 0;
	}
	catch (std::exception& ex)
	{
		printf("[lock::lock]%s\n",ex.what());
		throw exception(std::string(ex.what()));
	}
	addref("lock::lock");
}

lock::~lock()
{
}

void lock::get_stream_details(codec_details* det)
{
	try
	{

		//for(int i=0; i<_streams_count; i++)
		//{
		//	det[i] = _details[i];
		//	det[i].extra_data.resize( det[i].extra_data_size );
		//	std::memcpy( &det[i].extra_data.front() , &_details[i].extra_data.front(), det[i].extra_data_size );
		//}

		file_mapping content_file( file_name.c_str(), read_write);
		mapped_region region( content_file, read_only  );
		char* addr       = (char*)region.get_address();

		uint8_t _streams_count = 0;
		uint64_t size = sizeof(uint8_t);

		std::memcpy( &_streams_count, addr, sizeof(uint8_t) );
		addr += sizeof(uint8_t);

		for(int i=0; i<_streams_count; i++)
		{
			std::memcpy( &det[i], addr, sizeof(base_codec_details) );
			addr += sizeof(base_codec_details);

			std::memcpy( &det[i].extra_data_size, addr, sizeof(uint32_t) );
			addr += sizeof(uint32_t);

			det[i].extra_data.resize( det[i].extra_data_size );
			std::memcpy( &det[i].extra_data.front() , addr, det[i].extra_data_size );
			addr += det[i].extra_data_size;

			size += sizeof(base_codec_details) + sizeof(uint32_t) + det[i].extra_data_size;
		}
	}
	catch (std::exception& ex) {
		printf("[lock::get_stream_details]%s\n",ex.what());
		throw exception(std::string(ex.what()));
	}
	_CrtCheckMemory();
}


icontent_reader* lock::read(uint32_t* frame_no, //first frame returned by get_next_frame()
		uint64_t* timestamps, bool to_navigate_to_keyframe) //shifts in ms
{
	addref("lock::read");
	return this;
}

bool lock::change(range_t new_range)
{
	try
	{
		char* addr = (char*) _offsets->get_address();

		uint32_t frames_count = 0;
		std::memcpy( &frames_count, addr, sizeof(uint32_t) );
		addr += sizeof(uint32_t);

		std::vector<int64_t> shifts( frames_count, -LONG_MAX);
		std::memcpy( &shifts.front(), addr, frames_count * sizeof(int64_t) );

		bool acceptive = false;
		for(int i=new_range.start; i!=(new_range.end+1); i++)
			if( shifts.at( i )!=-LONG_MAX )
			{
				acceptive = true;
				range.start = i;
				break;
			}

		if( acceptive )
			for(int i=range.start+1; i!=(new_range.end+1); i++)
				if( shifts.at(i)==-LONG_MAX )
				{
					range.end = i - 1;
					break;
				}

		return acceptive;
	}
	catch (std::exception& ex)
	{
		printf("[lock::change]%s\n",ex.what());
		throw exception(std::string(ex.what()));
	}
	//if( !_cache_reader )
	//	return false;

	//_cache_reader->change( range );
	//return false;
}

range_t lock::get_range()
{
//	range_t r={0,0};
	return range;
}

bool lock::get_next_frames(std::vector<iframe*>& frames)
{
	_CrtCheckMemory();
	try
	{
		if( _frame_no>range.end || _frame_no<range.start )
			return false;

		char* addr = (char*) _offsets->get_address();

		uint32_t frames_count = 0;
		std::memcpy( &frames_count, addr, sizeof(uint32_t) );
		addr += sizeof(uint32_t);

		std::vector<int64_t> shifts( frames_count, -LONG_MAX);
		std::memcpy( &shifts.front(), addr, frames_count * sizeof(int64_t) );

		uint64_t _offset = shifts.at( _frame_no++ );
		if( _offset==-LONG_MAX )
			return false;




		uint32_t size = 0;
		uint32_t count = 0;


		boost::interprocess::mapped_region* region = new mapped_region( *content_file, read_only, _offset , 2 * sizeof(uint32_t) );
		char* offset       = (char*) region->get_address();

		std::memcpy( &count, offset, sizeof(uint32_t));
		offset += sizeof(uint32_t);

		std::memcpy( &size, offset, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		
		delete region;




		region = new mapped_region( *content_file, read_only, _offset , size );
		offset       = (char*) region->get_address() + (2* sizeof(uint32_t));

		frames.clear();

		icontent_writer::frame_data fd;
		for(int i=0; i!=count; i++)
		{

			std::auto_ptr<cache_frame> frame(new cache_frame);

			std::memcpy( &fd, offset, sizeof(icontent_writer::frame_data));
			offset += sizeof(icontent_writer::frame_data);

			frame->buf = buf_t::alloc( fd.len );
			frame->buf->addref("cache_frame::get_next_frame");
			frame->buf->size = fd.len ;

			std::memcpy( frame->buf->data, offset, fd.len);
			offset += fd.len;

			frame->_stream_no = fd.stream_no;
			frame->_timestamp = _streams_positions[fd.stream_no];
			frame->duration = fd.duration; 

			_streams_positions[fd.stream_no] += fd.duration ;

			frames.push_back( frame.release() );
		}

		delete region;

		_CrtCheckMemory();

		return true;
	}
	catch(std::exception& ex)
	{
		printf("[dummy_content_reader::get_next_frames]%s(frame:%d)\n", ex.what(), _frame_no);
		throw exception(std::string(ex.what()));
	}

	_CrtCheckMemory();

	return false;
}

void lock::release()
{
	refcounted_obj::release("lock::release");
//	release("lock::release");
//	delete this;
}




/*
dummy_content_reader::dummy_content_reader(  const char* file_name, uint32_t frame_start, uint32_t frame_end)//,
//										   uint32_t* frame_no, uint64_t* timestamps )
{
	try
	{
		content_file.reset( new file_mapping(file_name, read_only) );

	//	printf("  lock...");

		range.start =	frame_start;
		range.end	=	frame_end;

		_frame_no = 0;
		//_frame_no = *frame_no;
		//_timestamps = timestamps;

		mapped_region region( *content_file, read_only  );
		char* addr       = (char*)region.get_address();

		uint32_t extra_size = 0;
		uint8_t _stream_count = 0;
		uint64_t size = sizeof(uint8_t);

		std::memcpy( &_streams_count, addr, sizeof(uint8_t) );
		addr += sizeof(uint8_t);

		for(int i=0; i<_streams_count; i++)
		{
			addr += sizeof(base_codec_details);
			std::memcpy( &extra_size, addr, sizeof(uint32_t) );
			addr += sizeof(uint32_t) + extra_size;

			size += sizeof(base_codec_details) + sizeof(uint32_t) + extra_size;
		}



		uint32_t frames_count = 0;
		std::memcpy( &frames_count, addr, sizeof(uint32_t) );
		addr += sizeof(uint32_t);

		_offsets.reset( new mapped_region( *content_file, read_only, size,
						frames_count * sizeof(uint64_t) + sizeof(uint32_t) ) );

		range_t r={frame_start, frame_end};
		if( !change(r) )
			throw exception("i cant' lock this range\n");


		printf( " lock has been created![%d:%d]\n", range.start, range.end );

		_streams_positions.resize( _streams_count );
		for(int i=0; i<_streams_count; i++)
			_streams_positions[i] = 0;


	}
	catch (interprocess_exception& ex)
	{
		printf("[dummy_content_reader::dummy_content_reader]%s\n",ex.what());
		throw exception(std::string(ex.what()));
	}
}

dummy_content_reader::~dummy_content_reader()
{
}

bool dummy_content_reader::change(range_t new_range)
{
	try
	{

		char* addr = (char*) _offsets->get_address();

		uint32_t frames_count = 0;
		std::memcpy( &frames_count, addr, sizeof(uint32_t) );
		addr += sizeof(uint32_t);

		std::vector<int64_t> shifts( frames_count, -LONG_MAX);
		std::memcpy( &shifts.front(), addr, frames_count * sizeof(int64_t) );

		bool acceptive = false;
		for(int i=new_range.start; i!=(new_range.end+1); i++)
			if( shifts.at( i )!=-LONG_MAX )
			{
				acceptive = true;
				range.start = i;
				break;
			}

		if( acceptive )
			for(int i=range.start+1; i!=(new_range.end+1); i++)
				if( shifts.at(i)==-LONG_MAX )
				{
					range.end = i - 1;
					break;
				}

		return acceptive;
	}
	catch (interprocess_exception& ex)
	{
		printf("[dummy_content_reader::change]%s\n",ex.what());
		throw exception(std::string(ex.what()));
	}
}

range_t dummy_content_reader::get_range()
{
	return range;
}



void dummy_content_reader::release()
{
	_CrtCheckMemory();
	delete this;
	_CrtCheckMemory();
}
*/
}
