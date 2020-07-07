#pragma once

#include "cache_structs.h"
#include "icache.h"
#include "context_base.h"
#include "media_files_reader.h"

#include <netsim/pthread_holders.h>

namespace peer_app
{

namespace cache
{

class cache:
	public context_base,
	public icache
{
	void do_list(cache_state_t&);
public:
	cache(application*);
	~cache(void);

	boost::shared_ptr<compass_t> compass;
	hmutex compass_mutex;

	media_files_reader		_media_files_reader;

	std::string cache_path;
	boost::scoped_ptr<std::ofstream> log;
	cache_state_t cur_cs;

	bool delete_cache(const content_id_t&);
	bool check_space(const content_id_t&);
	
	// icache virtuals:
	virtual ilock* lock(
		const content_id_t&,
		const uint8_t* streams,uint8_t streams_count, //absolute stream numbers
		const ts_range_t&);

	virtual iwriter* write(
		const content_id_t&,
		const uint8_t* streams,uint8_t streams_count, //absolute stream numbers
		const codec_details*, //array of codec_details
		uint64_t total_duration, //
		const uint64_t* timestamps
		);

	virtual void list(cache_state_t&);

	virtual void get_diff(cache_diff_t&);

	virtual void delete_content(const content_id_t& cid);

	virtual void add_content(const content_id_t&, const std::string& path, const public_key_t& public_key);

	virtual void commit();

	virtual media_files_reader* get_media_files_reader() { return &_media_files_reader; }

	virtual void release();
};

} }

