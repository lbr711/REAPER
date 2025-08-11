#pragma once

#include "dpdkAppUtility.hpp"

// after starting parser, start assembler
// #define SPLIT_START
#define RSS_BETTER_BALANCE

// #define START_PARSER_ONLY


namespace Reaper
{

// forward Declaration
class ParserWorkerThread; 
class AssemblerWorkerThread;
class AggregatorWorkerThread;
class InspectorWorkerThread;
class DetectorWorkerThread;

// the Configuration Parameters for Reaper runtime env
struct DpdkRuntimeEnvParam final {

    // Dpdk Device port vec
    vector<nic_port_id_t> dpdk_port_vec; 

    // Dpdk Device的Rx/Tx队列数量
    nic_queue_id_t rx_queue_num = 0;
    nic_queue_id_t tx_queue_num = 0;

    // 分配的内存池的大小
    mem_pool_size_t mem_pool_size = 4096 * 4 - 1;

    // 使用的cpu核心core数量
    cpu_core_id_t parser_cores_num = 0;
    cpu_core_id_t assembler_cores_num = 0;
    cpu_core_id_t inspector_cores_num = 0;
    cpu_core_id_t aggregator_cores_num = 0;
    cpu_core_id_t detector_cores_num = 0;
    cpu_core_id_t dpdk_cores_num = 0;

    void inline display_params() const {
        
        printf("[ ***DpdkRuntimeEnvParam*** ]\n");

        printf("[DPDK Devices] -> Rx Queue Num: %d, Tx Queue Num: %d\n", rx_queue_num, tx_queue_num);
        printf("[DPDK Devices] -> DPDK Port: ");
        for (size_t i; i < dpdk_port_vec.size(); i ++) {
            if (i != 0) printf(", ");
            printf("%d", static_cast<uint32_t >(dpdk_port_vec[i]));
        }
        printf("\n");

        printf("[Memory] -> Memory Pool Size: %d\n", mem_pool_size);

        printf("[CPU] -> Occupied Core Num: %d (%d for parser, %d for assembler)\n", dpdk_cores_num, parser_cores_num, assembler_cores_num);
    }

    DpdkRuntimeEnvParam() {}
    virtual ~DpdkRuntimeEnvParam() {}
    DpdkRuntimeEnvParam & operator=(const DpdkRuntimeEnvParam &) = delete;
    DpdkRuntimeEnvParam(const DpdkRuntimeEnvParam &) = delete;
     
};

// reserve thread state before destroying them
struct ThreadStateMonitor final {

	bool stop = true;

	vector<shared_ptr<ParserWorkerThread> > parser_worker_thread_vec;
    vector<shared_ptr<AssemblerWorkerThread> > assembler_worker_thread_vec;
    vector<shared_ptr<InspectorWorkerThread > > inspector_worker_thread_vec;
    vector<shared_ptr<AggregatorWorkerThread > > aggregator_worker_thread_vec;
    vector<shared_ptr<DetectorWorkerThread> > detector_worker_thread_vec;

	ThreadStateMonitor() = default;
    virtual ~ThreadStateMonitor() {}
    ThreadStateMonitor & operator=(const ThreadStateMonitor &) = default;
    ThreadStateMonitor(const ThreadStateMonitor &) = default;

    ThreadStateMonitor(const decltype(parser_worker_thread_vec) & _p_vec,
                          const decltype(assembler_worker_thread_vec) & _a_vec,
                          const decltype(inspector_worker_thread_vec) & _i_vec,
                          const decltype(aggregator_worker_thread_vec) & _ag_vec,
                          const decltype(detector_worker_thread_vec) & _d_vec): 
                          parser_worker_thread_vec(_p_vec), 
                          assembler_worker_thread_vec(_a_vec), 
                          inspector_worker_thread_vec(_i_vec),
                          aggregator_worker_thread_vec(_ag_vec),
                          detector_worker_thread_vec(_d_vec), stop(false) {}

};

class ConfigReaper final {

// private members (can be accesssed by the class itself or friend class)
private:

    // Parameters of runtime Reaper
    shared_ptr<DpdkRuntimeEnvParam > p_dpdk_runtime_env_param;

    // // Report details or not
    // bool verbose = true;    

    // Whether already have initialized the DPDK runtime environment
    mutable bool dpdk_runtime_env_init_once = false;

    // configure runtime environment of dpdk app (occupy CPU cores and obtain dpdk devices)
    // return a list including all the devices which are bound with dpdk driver
    vector<DpdkDevice *> configure_dpdk_runtime_env(const CoreMask mask_dpdk_occupied_cores) const;
    
    // map rx queues of all dpdk devices to cores for parsing, each map records a subset of rx queues for each dpdk device
    // return a list including all the map  
    dpdk_dev_map_list_t bind_rx_queue_to_cores(const vector<DpdkDevice *> dpdk_dev_list, const vector<SystemCore> & parser_cores) const ;
    
    // create threads including parsers and assemblers
    // return success or fail 
    bool create_worker_threads(const dpdk_dev_map_list_t dpdk_dev_map_list, 
                                    vector<shared_ptr<ParserWorkerThread > > & parser_thread_vec, 
                                    vector<shared_ptr<AssemblerWorkerThread > > & assembler_thread_vec,
                                    vector<shared_ptr<InspectorWorkerThread > > & inspector_thread_vec,
                                    vector<shared_ptr<AggregatorWorkerThread > > & aggregator_thread_vec,
                                    vector<shared_ptr<DetectorWorkerThread > > & detector_thread_vec);

    // take actions when dpdk app is interrupted
    static void interrupt_callback(void* cookie);

    // subsets of runtime Reaper parameters 
    json j_parser_params;
    json j_assembler_params;
    json j_inspector_params;
    json j_aggregator_params;
    json j_detector_params;
    // ... loading ...

public:

    ConfigReaper(const decltype(p_dpdk_runtime_env_param) _p):p_dpdk_runtime_env_param(_p) {
        LOGF("Load specified parameters for DPDK Runtime Environment.");
    } 

    ConfigReaper(const json & jin) {
        if (load_params_via_json(jin)) {
            LOGF("Load Parameters for DPDK Runitme Environment with Specified JSON File Successfully. ");
        } else {
            LOGF("Fail to Load Parameters for DPDK Runtime Environment with Specified Invalid JSON File! ");
        }
    }

    virtual ~ConfigReaper(){}
    ConfigReaper & operator=(const ConfigReaper &) = delete;
    ConfigReaper(const ConfigReaper &) = delete;

    bool load_params_via_json(const json & jin);
    void enable_reaper(); 

};

}