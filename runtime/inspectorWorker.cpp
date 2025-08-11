#include "inspectorWorker.hpp"
#include "assemblerWorker.hpp"

using namespace Reaper;

static inline double_t __get_double_ts() {

    struct timeval ts;
    gettimeofday(&ts, nullptr);
    return ts.tv_sec + ts.tv_usec*(1e-6);

}

bool InspectorWorkerThread::run(uint32_t coreId) {

	if (p_assembler_vec.size() == 0) {

        WARN("No Assemblers are Bound to Current Inspector.");
        
        return false;

    }

	LOGF("Inspector on Core #%d Start", coreId);

	m_stop = false;
	m_core_id = coreId;

	unique_ptr<vector<FlowID > > last_pool;
    uint64_t snapshot_ts;

    auto inspect_pool = [&] (const auto & pool, auto & flow_tbl) -> void {

        if (!pool || !next_historical_pool) return;

        tbb::parallel_for_each(pool->begin(), pool->end(), [&] (const FlowID & _id) {

            FlowTable::accessor acc;

            if (flow_tbl.find(acc, _id)) {

                FlowEntry _entry = acc->second;

                if ((_entry.dirs[0].last_ts <= snapshot_ts && (snapshot_ts - _entry.dirs[0].last_ts >= p_inspector_param->idle_time_out)) || 
                                                                snapshot_ts - _entry.dirs[0].first_ts >= p_inspector_param->hard_time_out) {

                    // 当前流已经完成, 从流表中驱逐
                    flow_tbl.erase(acc); 

                    // 长短流分类
                    if (_entry.dirs[0].len >= p_inspector_param->long_th) { 
                        
                        shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(_entry.dirs[0].p_flat_vec, _entry.dirs[0].vol);

                        long_queue.push(p0); 
                    
                    } else { 
                        
                        short_flow_queue.push({_id, move(_entry)}); 
                    
                    }

                } else {

                    next_historical_pool->insert(_id);

                }

            }

        });

    };

    while (!m_stop) {

        next_historical_pool = make_unique<tbb::concurrent_unordered_set<FlowID, FlowIDHash > >();

        // next_historical_pool = make_unique<unordered_set<FlowID, FlowIDHash > >();

        // for (size_t i = 0; i < p_assembler_vec.size(); i ++) {

        //     if (p_assembler_vec[i]->last_pool_queue.try_pop(last_pool) && p_assembler_vec[i]->snapshot_ts_queue.try_pop(snapshot_ts)) {

        //         for (const auto & _id : *last_pool) {

        //             FlowTable::accessor acc;

        //             if (p_assembler_vec[i]->flow_tbl.find(acc, _id)) {

        //                 FlowEntry _entry = acc->second;

        //                 if ((_entry.dirs[0].last_ts <= snapshot_ts && (snapshot_ts - _entry.dirs[0].last_ts >= p_inspector_param->idle_time_out)) || 
        //                                                                 snapshot_ts - _entry.dirs[0].first_ts >= p_inspector_param->hard_time_out) {

        //                     // 当前流已经完成, 从流表中驱逐
        //                     p_assembler_vec[i]->flow_tbl.erase(acc); 

        //                     // 长短流分类
        //                     if (_entry.dirs[0].len >= p_inspector_param->long_th) { 
                                
        //                         shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(_entry.dirs[0].p_flat_vec, _entry.dirs[0].vol);

        //                         long_queue.push(p0); 
                            
        //                     } else { 
                                
        //                         short_flow_queue.push({_id, move(_entry)}); 
                            
        //                     }

        //                 } else {

        //                     next_historical_pool->insert(_id);

        //                 }

        //             }

        //         }

        //         for (const auto & _id : *historical_pool) {

        //             FlowTable::accessor acc;

        //             if (p_assembler_vec[i]->flow_tbl.find(acc, _id)) {

        //                 FlowEntry _entry = acc->second;

        //                 if ((_entry.dirs[0].last_ts <= snapshot_ts && (snapshot_ts - _entry.dirs[0].last_ts >= p_inspector_param->idle_time_out)) || 
        //                                                                 snapshot_ts - _entry.dirs[0].first_ts >= p_inspector_param->hard_time_out) {

        //                     // 当前流已经完成, 从流表中驱逐
        //                     p_assembler_vec[i]->flow_tbl.erase(acc); 

        //                     // 长短流分类
        //                     if (_entry.dirs[0].len >= p_inspector_param->long_th) { 
                                
        //                         shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(_entry.dirs[0].p_flat_vec, _entry.dirs[0].vol);

        //                         long_queue.push(p0); 
                            
        //                     } else { 
                                
        //                         short_flow_queue.push({_id, move(_entry)}); 
                            
        //                     }

        //                 } else {

        //                     next_historical_pool->insert(_id);

        //                 }

        //             }

        //         }

        //     } 

        // }     

        // historical_pool = move(next_historical_pool);       

        // usleep(50000);

        for (size_t i = 0; i < p_assembler_vec.size(); i ++) {

            if (p_assembler_vec[i]->last_pool_queue.try_pop(last_pool) && p_assembler_vec[i]->snapshot_ts_queue.try_pop(snapshot_ts)) {

                next_historical_pool = make_unique<tbb::concurrent_unordered_set<FlowID, FlowIDHash > >();
                
                try {

                    tbb::parallel_invoke(
                        [&]() { inspect_pool(historical_pool, p_assembler_vec[i]->flow_tbl); },
                        [&]() { inspect_pool(last_pool, p_assembler_vec[i]->flow_tbl); }
                    );

                } catch (const std::exception& e) {
                    
                    std::cerr << "Error during pool inspection: " << e.what() << std::endl;
                
                }

            } 

            historical_pool = move(next_historical_pool);

        }

    }


	return true;

}


void InspectorWorkerThread::stop() {

	LOGF("Inspector on Core #%d Stop", m_core_id);
	
	m_stop = true;
	
}

void InspectorWorkerThread::load_params_via_json(const json &jin) {

	if (p_inspector_param != nullptr) {

		WARN("Already Load Inspector Parameters.");

	} 

	try {

		p_inspector_param = make_shared<InspectorThreadParam >();

	} catch (exception & e) {

		FATAL_ERROR("Bad Memory Allocation for Inspector Parameters.");

	}

	try {

        if (jin.count("idle_time_out")) {
            p_inspector_param->idle_time_out = static_cast<decltype(p_inspector_param->idle_time_out)>(jin["idle_time_out"]);
        } else {
            FATAL_ERROR("Parameter(idle_time_out) is Missing!");
        }

        if (jin.count("hard_time_out")) {
            p_inspector_param->hard_time_out = static_cast<decltype(p_inspector_param->hard_time_out)>(jin["hard_time_out"]);
        } else {
            FATAL_ERROR("Parameter(hard_time_out) is Missing!");
        }

        if (jin.count("trunc_flow_len")) {
            p_inspector_param->trunc_flow_len = static_cast<decltype(p_inspector_param->trunc_flow_len)>(jin["trunc_flow_len"]);
        } else {
            FATAL_ERROR("Parameter(trunc_flow_len) is Missing!");
        }

        if (jin.count("long_th")) {
            p_inspector_param->long_th = static_cast<decltype(p_inspector_param->long_th)>(jin["long_th"]);
        } else {
            FATAL_ERROR("Parameter(long_th) is Missing!");
        }
    
    
    } catch (exception & e) {

        FATAL_ERROR(e.what());
    
    }

	return;

}