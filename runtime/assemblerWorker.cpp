#include "assemblerWorker.hpp"
#include "parserWorker.hpp"

using namespace Reaper;
using namespace pcpp;


static inline double_t __get_double_ts() {

    struct timeval ts;
    gettimeofday(&ts, nullptr);
    return ts.tv_sec + ts.tv_usec*(1e-6);

}

static inline uint64_t __get_uint64_ts() {

    struct timeval ts;
    gettimeofday(&ts, nullptr);
    return (uint64_t)(ts.tv_sec * 1e6 + ts.tv_usec);

}

bool AssemblerWorkerThread::run(uint32_t coreId) {

    if (p_parser_vec.size() == 0) {

        WARN("No Parsers are Bound to Current Assembler.");
        
        return false;

    }

    // make_shared不能分配动态数组的空间, 一般是使用shared_ptr的构造函数进行显示转换
    pkt_meta_buffer = shared_ptr<PacketMetaData[]>(new PacketMetaData[p_assembler_param->pkt_meta_buffer_size](), 
                                                    std::default_delete<PacketMetaData[]>());

    if (pkt_meta_buffer == nullptr) {

        WARN("Bad Memory Allocation for Packet Metadata.");

        return false;

    }

    m_core_id = coreId;
    m_stop = false;

    double_t last_ts = __get_double_ts();

    last_enqueue_ts = __get_uint64_ts();

    while (!m_stop) {

        double_t curr_ts = __get_double_ts();
        double_t delta_time = (curr_ts - last_ts);

        if (delta_time > p_assembler_param->report_interval) {

            if (p_assembler_param->tracing_mode) {

                double_t curr_fetch_throughput, curr_update_throughput;

                if (sum_update_pkt_len == 0) curr_update_throughput = 0;
                else curr_update_throughput = (((double_t) sum_update_pkt_len) * 8.0) / update_active_time / 1e9; 

                if (sum_update_pkt_len == 0) curr_fetch_throughput = 0;
                else curr_fetch_throughput = (((double_t) sum_update_pkt_len) * 8.0) / fetch_active_time / 1e9; 


                LOGF("Assembler (Fetching) on Core #%d: [ %4.5lf Gbps ]", m_core_id, curr_fetch_throughput);
                LOGF("Assembler (Updateing) on Core #%d: [ %4.5lf Gbps ]", m_core_id, curr_update_throughput);

            }

            last_ts = curr_ts;

        }

        size_t sum_fetch = 0;

        double_t fetch_start_ts = __get_double_ts();

        for (size_t i = 0; i < p_parser_vec.size(); i ++) {

            p_parser_vec[i]->acquire_semaphore();
            sum_fetch += fetch_from_parser(p_parser_vec[i]);
            p_parser_vec[i]->release_semaphore();

        }

        double_t fetch_end_ts = __get_double_ts();

        // sum_fetch_pkt_len += sum_fetch;
        fetch_active_time += (fetch_end_ts - fetch_start_ts);


        // double_t update_start_ts = __get_double_ts();

        update_flow_tbl();

        // double_t update_end_ts = __get_double_ts();

        // update_active_time += (update_end_ts - update_start_ts);

        // usleep(50000);
        // usleep(p_assembler_param->pause_time);
    
    }

    return true;

}

void AssemblerWorkerThread::update_flow_tbl() {

    const size_t cur_buffer_count = buffer_next;
    PacketMetaData* const cur_pkt_meta = pkt_meta_buffer.get();

    for (size_t i = 0; i < cur_buffer_count; i ++) {

        uint64_t curr_ts = __get_uint64_ts(); 
        
        if (curr_ts - last_enqueue_ts > p_assembler_param->pause_time) {

            last_pool_queue.push(move(next_pool));
            snapshot_ts_queue.push(curr_ts);

            next_pool = make_unique<vector<FlowID > >();

            last_enqueue_ts = curr_ts;

        }

        uint32_t src_ip = cur_pkt_meta[i].src_ip;
        uint32_t dst_ip = cur_pkt_meta[i].dst_ip;
        uint32_t src_port = cur_pkt_meta[i].src_port;
        uint32_t dst_port = cur_pkt_meta[i].dst_port;
        uint8_t proto = cur_pkt_meta[i].proto;
        uint32_t pkt_attr = cur_pkt_meta[i].pkt_attr;
        uint16_t pkt_length = cur_pkt_meta[i].pkt_length;
        uint64_t arr_time_stamp = cur_pkt_meta[i].time_stamp;

        // 判定是前向流还是后向流
        bool forward_direction = true;

        if (src_ip > dst_ip) { swap(src_ip, dst_ip); swap(src_port, dst_port); forward_direction = false; }

        if (forward_direction) { pkt_attr = pkt_attr | 0x000000ff; }

        FlowID _id = {src_ip, dst_ip, src_port, dst_port, proto};

        FlowTable::accessor acc;

        double_t update_start_ts = __get_double_ts();
        
        if (!flow_tbl.find(acc, _id)) {

            // 如果TCP流的首个数据包不是SYN数据包, 该TCP不完整, 不加入flow_tbl
            // cancel for throughput measuring
            // if (proto == 0x06 && (pkt_attr & 0x0000ff00) != 0x00000200) { continue; }  

            FlowEntry _entry = FlowEntry();
                        
            _entry.dirs[0].first_ts = arr_time_stamp;
            _entry.dirs[0].len = 1;
            _entry.dirs[0].vol = pkt_length;
            _entry.dirs[0].last_ts = arr_time_stamp;

            _entry.dirs[0].p_flat_vec->push_back(arr_time_stamp);
            _entry.dirs[0].p_flat_vec->push_back(pkt_length);
            _entry.dirs[0].p_flat_vec->push_back(pkt_attr);

            if (forward_direction) {

                _entry.forward_init = true;

                _entry.dirs[1].first_ts = arr_time_stamp;
                _entry.dirs[1].len = 1;
                _entry.dirs[1].vol = pkt_length;
                _entry.dirs[1].last_ts = arr_time_stamp;

                _entry.dirs[1].p_flat_vec->push_back(arr_time_stamp);
                _entry.dirs[1].p_flat_vec->push_back(pkt_length);
                _entry.dirs[1].p_flat_vec->push_back(pkt_attr);

            } else {

                _entry.backward_init = true;

                _entry.dirs[2].first_ts = arr_time_stamp;
                _entry.dirs[2].len = 1;
                _entry.dirs[2].vol = pkt_length;
                _entry.dirs[2].last_ts = arr_time_stamp;

                _entry.dirs[2].p_flat_vec->push_back(arr_time_stamp);
                _entry.dirs[2].p_flat_vec->push_back(pkt_length);
                _entry.dirs[2].p_flat_vec->push_back(pkt_attr);

            }

            flow_tbl.insert(acc, _id);
            acc->second = move(_entry);

            next_pool->push_back(_id);

        } else {

            acc->second.dirs[0].len ++;
            acc->second.dirs[0].last_ts = arr_time_stamp;

            if (acc->second.dirs[0].len < p_assembler_param->trunc_flow_len) { 
                
                acc->second.dirs[0].vol += pkt_length;
                acc->second.dirs[0].p_flat_vec->push_back(arr_time_stamp);
                acc->second.dirs[0].p_flat_vec->push_back(pkt_length);
                acc->second.dirs[0].p_flat_vec->push_back(pkt_attr);

            } else {

                acc->second.dirs[0].vol += pkt_length;

            }

            if (forward_direction) {

                if (!acc->second.forward_init) { acc->second.forward_init = true; }
                
                if (acc->second.dirs[0].len < p_assembler_param->trunc_flow_len) {
                    
                    acc->second.dirs[1].vol += pkt_length;
                    acc->second.dirs[1].len ++;

                    acc->second.dirs[1].p_flat_vec->push_back(arr_time_stamp);
                    acc->second.dirs[1].p_flat_vec->push_back(pkt_length);
                    acc->second.dirs[1].p_flat_vec->push_back(pkt_attr);

                } else {

                    acc->second.dirs[1].vol += pkt_length;

                }

            } else {

                if (!acc->second.backward_init) { acc->second.backward_init = true; }
                
                if (acc->second.dirs[0].len < p_assembler_param->trunc_flow_len) {
                    
                    acc->second.dirs[2].vol += pkt_length;
                    acc->second.dirs[2].len ++;

                    acc->second.dirs[2].p_flat_vec->push_back(arr_time_stamp);
                    acc->second.dirs[2].p_flat_vec->push_back(pkt_length);
                    acc->second.dirs[2].p_flat_vec->push_back(pkt_attr);

                } else {

                    acc->second.dirs[2].vol += pkt_length;

                }
            }      

        }

        double_t update_end_ts = __get_double_ts();

        update_active_time += (update_end_ts - update_start_ts);

        sum_update_pkt_len += cur_pkt_meta[i].pkt_length;
        
    }
    
    buffer_next = 0; // 当前pkt_meta_buffer中所有pkt_meta都处理完毕

}

void AssemblerWorkerThread::stop() {
    
    LOGF("Assembler on Core #%d Stop.", m_core_id);

    m_stop = true;

    // sum_update_pkt_len += update_pkt_len;

}

size_t AssemblerWorkerThread::fetch_from_parser(const shared_ptr<ParserWorkerThread> pt) const {

    size_t fetch_count = 0;

    size_t first_chunk_size = 0;
    size_t second_chunk_size = 0;
    
    size_t cur_ring_queue_count = pt->ring_queue_count;

    if (cur_ring_queue_count == 0) return 0;

    // calculate fetch_count
    size_t buffer_available_space = p_assembler_param->pkt_meta_buffer_size - buffer_next;
    fetch_count = min(cur_ring_queue_count, min(buffer_available_space, p_assembler_param->max_fetch));

    // fetch
    if (pt->ring_queue_begin < pt->ring_queue_end) {
        
        // dequeue fetch_count elements
        memcpy(pkt_meta_buffer.get() + buffer_next, pt->ring_queue.get() + pt->ring_queue_begin, fetch_count * sizeof(PacketMetaData));
        pt->ring_queue_begin += fetch_count;

    } else {

        if (pt->ring_queue_begin + fetch_count >= pt->ring_queue_size) {
            
            first_chunk_size = pt->ring_queue_size - pt->ring_queue_begin;
            second_chunk_size = (pt->ring_queue_begin + fetch_count) % pt->ring_queue_size;

            // dequeue fetch_count elements
            memcpy(pkt_meta_buffer.get() + buffer_next, pt->ring_queue.get() + pt->ring_queue_begin, first_chunk_size * sizeof(PacketMetaData));
            memcpy(pkt_meta_buffer.get() + buffer_next + first_chunk_size, pt->ring_queue.get(), second_chunk_size * sizeof(PacketMetaData));

            pt->ring_queue_begin = (pt->ring_queue_begin + fetch_count) % pt->ring_queue_size;

        } else {

            // dequeue fetch_count elements
            memcpy(pkt_meta_buffer.get() + buffer_next, pt->ring_queue.get() + pt->ring_queue_begin, fetch_count * sizeof(PacketMetaData));
            pt->ring_queue_begin += fetch_count;

        }

    }

    pt->ring_queue_count -= fetch_count;
    buffer_next += fetch_count;

    return fetch_count;

}

pair<double_t, double_t > AssemblerWorkerThread::get_overall_performance() const {

    if (!m_stop) {

		WARN("Assembling is Not Finished.");
		return {0.0, 0.0};
	
	}
    
    return {((((double_t) sum_update_pkt_len) * 8.0) /  (fetch_active_time)) / 1e9,
        ((((double_t) sum_update_pkt_len) * 8.0) /  (update_active_time)) / 1e9};

}

void AssemblerWorkerThread::load_params_via_json(const json & jin) {

    if (p_assembler_param != nullptr) {

		WARN("Already Load Assembler Parameters.");

	} 

	try {

		p_assembler_param = make_shared<AssemblerThreadParam >();

	} catch (exception & e) {

		FATAL_ERROR("Bad Memory Allocation for Assembler Parameters.");

	}

	try {

        if (jin.count("tracing_mode")) {
            p_assembler_param->tracing_mode = jin["tracing_mode"];
        } else {
            FATAL_ERROR("Parameter(tracing_mode) is Missing!");
        }

        if (jin.count("report_interval")) {
			p_assembler_param->report_interval = static_cast<decltype(p_assembler_param->report_interval)>(jin["report_interval"]);
		} else {
			FATAL_ERROR("Parameter(report_interval) is Missing!");
		}

        if (jin.count("pause_time")) {
            p_assembler_param->pause_time = static_cast<decltype(p_assembler_param->pause_time)>(jin["pause_time"]);
        } else {
            FATAL_ERROR("Parameter(puase_time) is Missing!");
        }

        if (jin.count("pkt_meta_buffer_size")) {
            p_assembler_param->pkt_meta_buffer_size = static_cast<decltype(p_assembler_param->pkt_meta_buffer_size)>(jin["pkt_meta_buffer_size"]);
            if (p_assembler_param->pkt_meta_buffer_size > MAX_PKT_META_BUFFER_SIZE) {
				FATAL_ERROR("Exceed Max Packet MetaData Buffer Size.");
			}
        } else {
            FATAL_ERROR("Parameter(pkt_meta_buffer_size) is Missing!");
        }

        if (jin.count("max_fetch")) {
            p_assembler_param->max_fetch = static_cast<decltype(p_assembler_param->max_fetch)>(jin["max_fetch"]);
        } else {
            FATAL_ERROR("Parameter(max_fetch) is Missing!");
        }

        if (jin.count("trunc_flow_len")) {
            p_assembler_param->trunc_flow_len = static_cast<decltype(p_assembler_param->trunc_flow_len)>(jin["trunc_flow_len"]);
        } else {
            FATAL_ERROR("Parameter(trunc_flow_len) is Missing!");
        }
    
    } catch (exception & e) {

        FATAL_ERROR(e.what());
    
    }

    return;

}