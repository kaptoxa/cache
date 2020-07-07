#include "stdafx.h"

#include "cache.h"
#include "exceptions.h"
#include "peer_peer_protocol.h"
#include "application.h"
#include "lock.h"
#include "cache_writer.h"
#include "settings.h"

#include <pthread_stuff.h>
#include <cid_ops.h>

#include <fstream>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>


#if defined(_DEBUG) && defined(_WIN32)
#define new DEBUG_NEW
#endif

const uint64_t add_size = 1048576;

namespace fs = boost::filesystem;


namespace peer_app
{

namespace cache
{

uint64_t file_size(const char* name)
{
	fs::path p( name );
	if ( fs::exists( p ) && !fs::is_directory( p ) )
		return fs::file_size(p);
	else
		return 0;
}

static void save_cache_state(const cache_state_t& last_cache_state);

using namespace boost::interprocess;
	
icache* make_icache(application* app)
{
	return new cache(app);
}

static boost::shared_ptr<cache_content_t> read_ranges(const char* path,netsim::netsim_pthreads::ipthreads* pthreads)
{
	boost::shared_ptr<cache_content_t> result(new cache_content_t(pthreads));
	try
	{
		uint64_t ts_size = file_size(path);

		file_mapping timestamps_fm(path, read_only);
		mapped_region region( timestamps_fm, read_only  );
		char* addr       = (char*)region.get_address();

		uint64_t ts = 0;
		frame_node fn;

		uint32_t version = 0;
		std::memcpy( &version, addr, sizeof(uint32_t) ); addr += sizeof(uint32_t);
		if( version!=cache_version )
			throw exception("wrong cache version");

		uint64_t size = sizeof(uint64_t);
		std::memcpy( &ts, addr, sizeof(uint64_t) ); addr += sizeof(uint64_t);
		result->cont_ranges->total_duration = ts;

		while( (size+(3*sizeof(uint64_t)))<=ts_size )
		{
			std::memcpy( &ts, addr, sizeof(uint64_t) ); addr += sizeof(uint64_t);
			std::memcpy( &fn.duration, addr, sizeof(fn.duration) );	addr += sizeof(fn.duration);
			std::memcpy( &fn.offset, addr, sizeof(fn.offset) );	addr += sizeof(fn.offset);

			result->shifts->insert( std::make_pair(ts,fn) );
			size += (3*sizeof(uint64_t));
		}

		ts_range_t crange={0,0};
		shifts_t::iterator it=result->shifts->begin(),end=result->shifts->end();
		crange.ts_start = it->first;
		crange.ts_end  = it->first + it->second.duration;
		while( ++it!=end )
		{
			if( crange.ts_end!=it->first )
			{
				result->cont_ranges->ranges.push_back( crange );
				crange.ts_start = it->first;
				crange.ts_end  = it->first + it->second.duration;
			}
			else
				crange.ts_end += it->second.duration;
		}
		result->cont_ranges->ranges.push_back( crange );

		sort( result->cont_ranges->ranges.begin(), result->cont_ranges->ranges.end() );
		return result;
	}
	catch (std::exception& ex)
	{
		throw exception(std::string(ex.what()));
	}
}

cache::cache(application* app):
	context_base(app),
	_media_files_reader(app),
	compass_mutex(pthreads)
{
	log.reset( new std::ofstream("cache.log"));


	fs::path full_path( fs::initial_path<fs::path>().native_directory_string() + "/cache", fs::native );
	if ( !fs::exists( full_path ) )
		if( !fs::create_directory( full_path ) )
		{
			if (app->log)
			       *app->log << "\nCan not create directory: " << full_path.native_directory_string().c_str() << "\n\n";
			throw exception("cannot create directory!");
		}

	cache_path = full_path.native_directory_string()+ "\\";

	compass.reset( new compass_t() );

	try
	{
		if ( fs::is_directory( full_path ) )
		{
			fs::directory_iterator end_iter;
			for ( fs::directory_iterator dir_itr( full_path ); dir_itr != end_iter;++dir_itr )
			{
					content_id_t _cid;
					string_to_cid( dir_itr->path().leaf().c_str(), _cid );
					std::string path = cache_path + dir_itr->path().leaf().c_str();

					if( strstr(path.c_str(),".timestamps") )
					{
						boost::shared_ptr<cache_content_t> cc = read_ranges(path.c_str(),pthreads);
						path = cache_path + cid_to_string(_cid) + ".frames";
						*cc->file_size = file_size(path.c_str());

						compass->insert(std::make_pair( _cid, cc));
					}
			}
		}
	}
	catch ( const std::exception & ex )
	{
		*log << " <<cache::cache>> " << ex.what() << "\n";
		log->flush();
		throw;
	}
}

cache::~cache(void)
{
}

ilock* cache::lock(const content_id_t& id,
		const uint8_t* streams,uint8_t streams_count, //absolute stream numbers
		const ts_range_t& range)
{
	fs::path p( cache_path + cid_to_string(id) + ".frames" );

	if( !fs::exists( p ) && !fs::is_directory( p ) )
		if (ilock* mf_lock=_media_files_reader.open(id,streams,streams_count,range))
			return mf_lock;

	if( ilock* c_lock = new peer_app::cache::lock(this, id, streams, streams_count,range))
		return c_lock;

	throw errcode_exception("no such content",
		peer_peer_protocol::errors::E_NO_SUCH_CONTENT_IN_CACHE);

	std::string cid_ctring=cid_to_string(id);

	return new peer_app::cache::lock(this, id, streams, streams_count,range);
}

iwriter* cache::write(const content_id_t& cid,
		const uint8_t* streams,uint8_t streams_count, //absolute stream numbers
		const codec_details* details,//array of codec_details 
		uint64_t total_duration,
		const uint64_t* timestamps
		)
{
	return new peer_app::cache::cache_writer( this, cid, details, streams_count, total_duration, timestamps );
}

void cache::release()
{
	delete this;
}

void cache::list(cache_state_t& cs)
{
	do_list(cs);

	cur_cs = cs;
	save_cache_state(cache_state_t());
}

void cache::do_list(cache_state_t& cs)
{
	cs.content_states.clear();
	_media_files_reader.list(cs);


	mutex_locker ml( compass_mutex );

	if( compass->empty() )
		return;
	for(compass_t::iterator it=compass->begin(),end=compass->end();it!=end;++it)
	{
		content_state_t cr;

		{
			mutex_locker ml( *it->second->ranges_mutex );
			cr.cid = it->first;
			cr.total_duration=it->second->cont_ranges->total_duration;
			for(ts_ranges_t::iterator mit = it->second->cont_ranges->ranges.begin(),
				end=it->second->cont_ranges->ranges.end();mit!=end;++mit)
				cr.ranges.push_back( *mit );
		}

		for(std::list<content_state_t>::iterator lit=cs.content_states.begin(),end=cs.content_states.end();lit!=end;++lit)
			if( 0 == memcmp(lit->cid.data, cr.cid.data, sizeof lit->cid.data) )
			{
				cs.content_states.erase( lit );
				break;
			}

		cs.content_states.push_back( cr );
	}
}

bool cache::delete_cache(const content_id_t &cid)
{
	bool locked = false;
	try
	{
		mutex_locker ml( compass_mutex );
		compass_t::iterator it=compass->find(cid);
		if (it!=compass->end())
		{
			{
				mutex_locker ml( *it->second->counter_mutex );
				locked = *it->second->counter!=0;
			}

			if( !locked )
			{
				std::string file_name = cache_path + cid_to_string(cid) + ".timestamps";
				if( remove( file_name.c_str() ) == -1 )
				{
					*log <<"i could not delete" << file_name.c_str() << " .\n";
					log->flush();
					return false;
				}

				file_name = cache_path + cid_to_string(cid) + ".frames";
				if( remove( file_name.c_str() ) == -1 )
				{
					*log <<"i could not delete" << file_name.c_str() << " .\n";
					log->flush();
					return false;
				}

				compass->erase(it);

				*log << file_name.c_str() <<" has been deleted.\n";
				log->flush();
				return true;
			}
		}
	}
	catch (std::exception& ex){
		*log << " <<cache::delete_cache>>" << ex.what() << "\n";
		log->flush();
		throw exception(std::string(ex.what()));
	}
	return false;
}

bool cache::check_space(const content_id_t &cid)
{
	uint64_t max_total_size = app->_settings->cache_size * 1024;//1048576;
	try
	{
		mutex_locker ml( compass_mutex );

		if( compass->empty() )
			throw;

		uint64_t total_size = 0;
		uint64_t min_size = 0;
		compass_t::iterator killer=compass->end();

		for(compass_t::iterator it=compass->begin(),end=compass->end();it!=end;++it)
		{
			total_size += *(it->second->file_size);

			mutex_locker ml( *it->second->counter_mutex );
			if( 0==*it->second->counter )
				if( !min_size )
					min_size = *(killer = it)->second->file_size;
				else
					if( *it->second->file_size<min_size )
						min_size = *(killer = it)->second->file_size;
		}

		if( (total_size + add_size)>max_total_size )
		{
			if( killer==compass->end() )
				return false;	

			std::string file_name = cache_path + cid_to_string(killer->first) + ".timestamps";
			if( remove( file_name.c_str() ) == -1 )
				return false;

			file_name = cache_path + cid_to_string(killer->first) + ".frames";
			if( remove( file_name.c_str() ) == -1 )
				return false;
			total_size -= *killer->second->file_size;
			compass->erase( killer );

			if( (total_size + add_size)<max_total_size )
			{
				*log << " cache space has been released successfully!\n";
				log->flush();
				return true;
			}
		}
	}
	catch (std::exception& ex){
		*log << " <<cache::check_space>>" << ex.what() << "\n";
		log->flush();
		throw exception(std::string(ex.what()));
	}
	return true;
}

void cache::delete_content(const content_id_t& cid)
{
	if (!delete_cache(cid))
		throw exception("delete_content failed");
}

static void load_cache_state(cache_state_t& last_cache_state)
{
	FILE* f=fopen("cache_state","rb");
	if (0==f)
		goto lb_err;
	uint32_t contents_size;
	if (1!=fread(&contents_size,sizeof(contents_size),1,f))
		goto lb_err;
	last_cache_state.content_states.clear();
	for (uint32_t n=0;n<contents_size;n++)
	{
		last_cache_state.content_states.push_back(content_state_t());
		content_state_t& q=last_cache_state.content_states.back();
		if (1!=fread(&q.cid,sizeof(q.cid),1,f))
			goto lb_err;
		uint32_t ranges_size;
		if (1!=fread(&ranges_size,sizeof(ranges_size),1,f))
			goto lb_err;
		q.ranges.resize(ranges_size);
		if (ranges_size)
			if (1!=fread(&q.ranges.front(),sizeof(range_t)*q.ranges.size(),1,f))
				goto lb_err;
	}
	fclose(f);
	return;
lb_err:
	if (f)
		fclose(f);
	last_cache_state.content_states.clear();
}

static void save_cache_state(const cache_state_t& last_cache_state)
{
	FILE* f=fopen("cache_state","wb");
	if (0==f)
		throw exception("server_backend::save_previous_cache_update: fopen");
	try
	{
		uint32_t contents_size=(uint32_t)last_cache_state.content_states.size();
		if (1!=fwrite(&contents_size,sizeof(contents_size),1,f))
			throw exception("server_backend::save_previous_cache_update: fwrite");
		for (std::list<content_state_t>::const_iterator it=last_cache_state.content_states.begin(),
			end=last_cache_state.content_states.end();it!=end;++it)
		{
			const content_state_t& q=*it;
			if (1!=fwrite(&q.cid,sizeof(q.cid),1,f))
				throw exception("server_backend::save_previous_cache_update: fwrite");
			uint32_t ranges_size=(uint32_t)q.ranges.size();
			if (1!=fwrite(&ranges_size,sizeof(ranges_size),1,f))
				throw exception("server_backend::save_previous_cache_update: fwrite");
			if (q.ranges.size())
				if (1!=fwrite(&q.ranges.front(),sizeof(range_t)*q.ranges.size(),1,f))
					throw exception("server_backend::save_previous_cache_update: fwrite");
		}
	}
	catch (...)
	{
		fclose(f);
		throw;
	}
	fclose(f);
}

static void collapse_ranges(cache_state_t& cs)
{
	for (std::list<content_state_t>::iterator it=cs.content_states.begin(),
		end=cs.content_states.end();it!=end;++it)
	{
		ts_range_vec_t& ranges=it->ranges;
		for (size_t nc=ranges.size(),n=nc-2;n<nc;n--)
		if (ranges[n].ts_end==ranges[n+1].ts_start)
		{
			ranges[n].ts_end=ranges[n+1].ts_end;
			ranges.erase(ranges.begin()+n+1);
		}
	}
}

inline bool get_intersection(const ts_range_t& r1,const ts_range_t& r2,ts_range_t& out)
{
	if (r2.ts_end<=r1.ts_start || r1.ts_end<=r2.ts_start)
		return false;
	if (r1.ts_start>r2.ts_start)
		out.ts_start=r1.ts_start; else
		out.ts_start=r2.ts_start;
	if (r1.ts_end>r2.ts_end)
		out.ts_end=r2.ts_end; else
		out.ts_end=r1.ts_end;
	return true;
}

static void get_diff(ts_range_vec_t ranges_new,ts_range_vec_t ranges_old)
{
	for (size_t n=0;n!=ranges_new.size();n++)
	{
		ts_range_t& r_new=ranges_new[n];
		for (size_t m=0;m!=ranges_old.size();m++)
		{
			ts_range_t& r_old=ranges_old[m];
			ts_range_t r_out;
			if (get_intersection(r_new,r_old,r_out))
			{
				if (r_old.ts_start!=r_out.ts_start &&
					r_old.ts_end!=r_out.ts_end)
				{
					ts_range_t r={r_out.ts_end,r_old.ts_end};
					r_old.ts_end=r_out.ts_start;
					ranges_old.push_back(r);
					m--;
				} else
				if (r_old.ts_start!=r_out.ts_start)
				{
					r_old.ts_end=r_out.ts_start;
				} else
				if (r_old.ts_end!=r_out.ts_end)
				{
					r_old.ts_start=r_out.ts_end;
				} else
				{
					ranges_old.erase(ranges_old.begin()+m);
					m--;
				}

				if (r_new.ts_start!=r_out.ts_start &&
					r_new.ts_end!=r_out.ts_end)
				{
					ts_range_t r={r_out.ts_end,r_new.ts_end};
					r_new.ts_end=r_out.ts_start;
					ranges_new.push_back(r);
				} else
				if (r_new.ts_start!=r_out.ts_start)
				{
					r_new.ts_end=r_out.ts_start;
				} else
				if (r_new.ts_end!=r_out.ts_end)
				{
					r_new.ts_start=r_out.ts_end;
				} else
				{
					ranges_new.erase(ranges_new.begin()+n);
					n--;
					goto lb_out;
				}
			}
		}
lb_out:;
	}
}


void cache::get_diff(cache_diff_t& diff)
{
	cache_state_t last_cs;
	load_cache_state(last_cs);
	do_list(cur_cs);
	collapse_ranges(cur_cs);

	for (std::list<content_state_t>::iterator it=cur_cs.content_states.begin(),
		end=cur_cs.content_states.end();it!=end;++it)
	{
		content_state_t* cr1=&*it;
		content_state_t* cr2=0;
		for (std::list<content_state_t>::iterator it=last_cs.content_states.begin(),
			end=last_cs.content_states.end();it!=end;++it)
		if (memcmp(&it->cid,&cr1->cid,sizeof it->cid)==0)
		{
			cr2=&*it;
			break;
		}
		if (0==cr2) //added content
		{
			diff.content_diffs.push_back(content_diff_t());
			content_diff_t& uc=diff.content_diffs.back();
			uc.cid=cr1->cid;
			uc.added_ranges.resize(cr1->ranges.size());
			if (cr1->ranges.empty())
			{
				range_t r2={0,0};
				uc.added_ranges.push_back(r2);
			} else
			for (size_t n=0,nc=cr1->ranges.size();n!=nc;n++)
			{
				uc.added_ranges[n].start = static_cast<uint32_t>(cr1->ranges[n].ts_start / 10000000);
				uc.added_ranges[n].end = static_cast<uint32_t>(cr1->ranges[n].ts_end / 10000000);
			}
		} else
		if (cr1->ranges.size()!=cr2->ranges.size() ||
			cr1->ranges.size()!=0 &&
			memcmp(&cr1->ranges.front(),&cr2->ranges.front(),sizeof(range_t)*cr2->ranges.size()))
		{
			diff.content_diffs.push_back(content_diff_t());
			content_diff_t& uc=diff.content_diffs.back();
			uc.cid=cr1->cid;
			peer_app::cache::get_diff(cr1->ranges,cr2->ranges);

			uc.added_ranges.resize(cr1->ranges.size());
			for (size_t n=0,nc=cr1->ranges.size();n!=nc;n++)
			{
				uc.added_ranges[n].start = static_cast<uint32_t>(cr1->ranges[n].ts_start / 10000000);
				uc.added_ranges[n].end = static_cast<uint32_t>(cr1->ranges[n].ts_end / 10000000);
			}

			uc.removed_ranges.resize(cr2->ranges.size());
			for (size_t n=0,nc=cr2->ranges.size();n!=nc;n++)
			{
				uc.removed_ranges[n].start = static_cast<uint32_t>(cr2->ranges[n].ts_start / 10000000);
				uc.removed_ranges[n].end = static_cast<uint32_t>(cr2->ranges[n].ts_end / 10000000);
			}
		}
	}

	//deleted content:
	for (std::list<content_state_t>::iterator it=last_cs.content_states.begin(),
		end=last_cs.content_states.end();it!=end;++it)
	{
		content_state_t* cr1=&*it;
		bool found=false;
		for (std::list<content_state_t>::iterator it=cur_cs.content_states.begin(),
			end=cur_cs.content_states.end();it!=end;++it)
		if (memcmp(&it->cid,&cr1->cid,sizeof it->cid)==0)
		{
			found=true;
			break;
		}
		if (!found)
		{
			diff.content_diffs.push_back(content_diff_t());
			content_diff_t& uc=diff.content_diffs.back();
			uc.cid=cr1->cid;
			range_t r2={0,0};
			uc.removed_ranges.push_back(r2);
		}
	}
}

void cache::commit()
{
	save_cache_state(cur_cs);
}

void cache::add_content(const content_id_t& cid, const std::string& path, const public_key_t& public_key)
{
	_media_files_reader.add_content(cid, path, public_key);
}

} }

