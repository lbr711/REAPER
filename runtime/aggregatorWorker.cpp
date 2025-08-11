#include "aggregatorWorker.hpp"
#include "inspectorWorker.hpp"

using namespace Reaper;

static inline double_t __get_double_ts() {

    struct timeval ts;
    gettimeofday(&ts, nullptr);
    return ts.tv_sec + ts.tv_usec*(1e-6);

}

bool AggregatorWorkerThread::run(uint32_t coreId) {

	if (p_inspector_vec.size() == 0) {

        WARN("No Inspectors are Bound to Current Aggregator.");
        
        return false;

    }

	LOGF("Aggregator on Core #%d Start", coreId);

	m_stop = false;
	m_core_id = coreId;

	unique_ptr<IPTrie > ip_trie;
    ip_trie = make_unique<IPTrie >(p_aggregator_param->shortest_prefix_len, 
                                   p_aggregator_param->aggr_len_th, 
                                   p_aggregator_param->trunc_flow_len,
                                   p_short_aggr_queue);

    thread aggregator(&AggregatorWorkerThread::aggregator_exec, this);
    aggregator.detach();

    pair<FlowID, FlowEntry > short_flow;

    uint32_t aggr_flow_num = 0;

    double_t last_ts = __get_double_ts();

    while (!m_stop) {

        double_t curr_ts = __get_double_ts();
        double_t delta_time = (curr_ts - last_ts);

        if (delta_time > p_aggregator_param->report_interval) {

            if (p_aggregator_param->tracing_mode) {

                double_t curr_throughput;

                if (sum_create_pkt_len == 0) curr_throughput = 0;
                else curr_throughput = (((double_t) sum_create_pkt_len) * 8.0) / create_active_time / 1e9; 

                create_throughput.push_back(curr_throughput);

                LOGF("Aggregator (Creating) on Core #%d: [ %4.5lf Gbps ]", m_core_id, curr_throughput);

            }

            last_ts = curr_ts;
        }

        for (size_t i = 0; i < p_inspector_vec.size(); i ++) {

            if (p_inspector_vec[i]->short_flow_queue.try_pop(short_flow)) {

            	if (short_flow.second.forward_init) { 
                
	                double_t round_start_ts = __get_double_ts();

	                ip_trie->insert(short_flow.first.low_ip, short_flow.second.dirs[1]); 
	                
	                double_t round_end_ts = __get_double_ts();

	                sum_create_pkt_len += short_flow.second.dirs[1].vol;
	                create_active_time += (round_end_ts - round_start_ts);
	                
	                aggr_flow_num ++; 
	            
	            }

	            if (short_flow.second.backward_init) { 

	                double_t round_start_ts = __get_double_ts();
	                
	                ip_trie->insert(short_flow.first.high_ip, short_flow.second.dirs[2]); 

	                double_t round_end_ts = __get_double_ts();

	                sum_create_pkt_len += short_flow.second.dirs[2].vol;
	                create_active_time += (round_end_ts - round_start_ts);

	                aggr_flow_num ++; 
	                
	            }
	            
	            if (aggr_flow_num >= p_aggregator_param->aggr_cycle) { // aggr_flow_num每次变化1或者2, 有可能会不等于aggr_cycle
	           
	                ip_trie_queue.push(move(ip_trie));
	        
	                ip_trie = make_unique<IPTrie >(p_aggregator_param->shortest_prefix_len, 
	                                   p_aggregator_param->aggr_len_th, 
                                       p_aggregator_param->trunc_flow_len,
	                                   p_short_aggr_queue);

	                aggr_flow_num = 0;

	            }

            }

        }

    }


	return true;

}

void AggregatorWorkerThread::aggregator_exec() {

    unique_ptr<IPTrie > ip_trie;

    uint32_t round = 0;

    double_t last_ts = __get_double_ts();

    while (!m_stop) {

        double_t curr_ts = __get_double_ts();
        double_t delta_time = (curr_ts - last_ts);

        if (delta_time > p_aggregator_param->report_interval) {

            if (p_aggregator_param->tracing_mode) {

                double_t curr_throughput;

                if (sum_aggr_pkt_len == 0) curr_throughput = 0;
                else curr_throughput = (((double_t) sum_aggr_pkt_len) * 8.0) / aggr_active_time / 1e9; 

                aggr_throughput.push_back(curr_throughput);

                LOGF("Aggregator (Aggregating) on Core #%d: [ %4.5lf Gbps ]", m_core_id, curr_throughput);

            }

            last_ts = curr_ts;

        }

        if (ip_trie_queue.try_pop(ip_trie)) { 
            
            double_t round_start_ts = __get_double_ts();

            uint32_t aggr_pkt_len = ip_trie->aggregate();

            double_t round_end_ts = __get_double_ts();

            sum_aggr_pkt_len += aggr_pkt_len;
            aggr_active_time += (round_end_ts - round_start_ts);

        }

        usleep(50000);

    }    

}



void AggregatorWorkerThread::stop() {

	LOGF("Aggregator on Core #%d Stop", m_core_id);
	
	m_stop = true;
	
}

pair<double_t, double_t> AggregatorWorkerThread::get_overall_performance() const {

	if (!m_stop) {

		WARN("Aggregating is Not Finished.");
		return {0.0, 0.0};
	
	}

	// 平均每条流的推断时间 mean_inference_time

	return {
        ((((double_t) sum_create_pkt_len) * 8.0) /  (create_active_time)) / 1e9,
        ((((double_t) sum_aggr_pkt_len) * 8.0) /  (aggr_active_time)) / 1e9
    };


}

void AggregatorWorkerThread::load_params_via_json(const json &jin) {

	if (p_aggregator_param != nullptr) {

		WARN("Already Load Aggregator Parameters.");

	} 

	try {

		p_aggregator_param = make_shared<AggregatorThreadParam >();

	} catch (exception & e) {

		FATAL_ERROR("Bad Memory Allocation for Aggregator Parameters.");

	}

	try {

        if (jin.count("tracing_mode")) {
            p_aggregator_param->tracing_mode = jin["tracing_mode"];
        } else {
            FATAL_ERROR("Parameter(tracing_mode) is Missing!");
        }

        if (jin.count("report_interval")) {
			p_aggregator_param->report_interval = static_cast<decltype(p_aggregator_param->report_interval)>(jin["report_interval"]);
		} else {
			FATAL_ERROR("Parameter(report_interval) is Missing!");
		}

        if (jin.count("trunc_flow_len")) {
            p_aggregator_param->trunc_flow_len = static_cast<decltype(p_aggregator_param->trunc_flow_len)>(jin["trunc_flow_len"]);
        } else {
            FATAL_ERROR("Parameter(trunc_flow_len) is Missing!");
        }

        if (jin.count("shortest_prefix_len")) {
            p_aggregator_param->shortest_prefix_len = static_cast<decltype(p_aggregator_param->shortest_prefix_len)>(jin["shortest_prefix_len"]);
        } else {
            FATAL_ERROR("Parameter(shortest_prefix_len) is Missing!");
        }

        if (jin.count("aggr_len_th")) {
            p_aggregator_param->aggr_len_th = static_cast<decltype(p_aggregator_param->aggr_len_th)>(jin["aggr_len_th"]);
        } else {
            FATAL_ERROR("Parameter(aggr_len_th) is Missing!");
        }

        if (jin.count("aggr_cycle")) {
            p_aggregator_param->aggr_cycle = static_cast<decltype(p_aggregator_param->aggr_cycle)>(jin["aggr_cycle"]);
        } else {
            FATAL_ERROR("Parameter(aggr_cycle) is Missing!");
        }
    
    
    } catch (exception & e) {

        FATAL_ERROR(e.what());
    
    }

	return;

}