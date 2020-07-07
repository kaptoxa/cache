#pragma once

#include <map>


static bool operator < (const content_id_t& a1,const content_id_t& a2)
{
	return memcmp(a1.data,a2.data,sizeof a1.data)>0;
}
static bool operator < (const uint64_t& a1, const ts_range_t& a2){	return a1 < a2.ts_end; }
static bool operator < (const ts_range_t& a1, const uint64_t& a2){ return a1.ts_end < a2; }
static bool operator < (const ts_range_t& a1, const ts_range_t& a2){ return a1.ts_end < a2.ts_end; }

const uint64_t hkf = 0x4000000000000000;
inline uint32_t crc(char *buf, int size) 
{
	int i, j;
	uint32_t tmp, flag;
	
	tmp = 0xFFFF;
	for (i = 0; i < size; i++)
	{
		tmp ^= buf[i];
		for (j = 1; j <= 8; j++)
		{
			flag = tmp & 0x0001;
			tmp >>= 1;
			if (flag) tmp ^= 0xA001;
		}
	}
	return tmp;
}




namespace peer_app
{

const uint32_t cache_version = 0x4305F6CE;

struct frame_node
{
	uint64_t duration;
	uint64_t offset;

	bool is_key_frame(){ return (offset & hkf)>0;}
	uint64_t get_offset(){ return (offset & hkf) ? (offset ^ hkf) : offset;}
};
typedef std::map<uint64_t,frame_node> shifts_t;

struct cache_content_t
{
	boost::shared_ptr<hmutex> ranges_mutex;
	boost::shared_ptr<content_state_t> cont_ranges;
	boost::shared_ptr<shifts_t> shifts;

	boost::shared_ptr<hmutex> enlarge_mutex;
	boost::shared_ptr<uint64_t> file_size;

	boost::shared_ptr<hmutex> counter_mutex;
	boost::shared_ptr<int32_t> counter;

	cache_content_t(netsim::netsim_pthreads::ipthreads* pthreads)
	{
		enlarge_mutex.reset( new hmutex( pthreads ) );
		ranges_mutex.reset( new hmutex( pthreads ) );
		counter_mutex.reset( new hmutex( pthreads ) );

		file_size.reset( new uint64_t(0) );
		counter.reset( new int32_t(0) );

		boost::shared_ptr<content_state_t> ncr( new content_state_t() );
		cont_ranges = ncr;

		boost::shared_ptr<shifts_t> ns( new shifts_t() );
		shifts = ns;
	}
};
typedef std::map<content_id_t,boost::shared_ptr<cache_content_t> > compass_t;


}

