#include "configReaper.hpp"
#include "aggregatorWorker.hpp"
#include "inspectorWorker.hpp"
#include "assemblerWorker.hpp"
#include "detectorWorker.hpp"
#include "parserWorker.hpp"
// #include "seriesCollector.hpp"

using namespace Reaper;
using namespace pcpp;

vector<DpdkDevice *> ConfigReaper::configure_dpdk_runtime_env(CoreMask mask_dpdk_occupied_cores) const {

	// apply for resources (cores, memory) for DPDK Runtime Environment
	if (dpdk_runtime_env_init_once)	{

		LOGF("DPDK Runtime Environment Has been Already Initialized. ");

	} else {

		if (!DpdkDeviceList::initDpdk(mask_dpdk_occupied_cores, p_dpdk_runtime_env_param->mem_pool_size)) {

			FATAL_ERROR("Fail to Initialize DPDK Runtime Environment! ");

		} else {

			dpdk_runtime_env_init_once = true;

		}

	}

	// probe all DPDK Devices counted by dpdk_port_vec
	vector<DpdkDevice *> dpdk_dev_list;

	for (size_t i = 0; i < p_dpdk_runtime_env_param->dpdk_port_vec.size(); i ++) {

		nic_port_id_t port_i = p_dpdk_runtime_env_param->dpdk_port_vec[i];
		DpdkDevice * dpdk_dev_i = DpdkDeviceList::getInstance().getDeviceByPort(port_i);

		if (dpdk_dev_i == nullptr) {

			string error_info = "Fail to Probe DPDK Device indexed by Port " + to_string(i);
			FATAL_ERROR(error_info);

		}

		dpdk_dev_list.push_back(dpdk_dev_i);

	}

	// Check all probed DPDK Devices whether support specified number of queue, Then Enable it

	DpdkDevice::DpdkDeviceConfiguration dpdk_dev_cfg;
	
	#ifdef RSS_BETTER_BALANCE

		dpdk_dev_cfg.rssKey = nullptr;
		dpdk_dev_cfg.rssKeyLength = 0;
		dpdk_dev_cfg.rssHashFunction = -1;

	#endif

	for (size_t i = 0; i < dpdk_dev_list.size(); i ++) {

		DpdkDevice* dpdk_dev_i = dpdk_dev_list[i];

		if (dpdk_dev_i->getTotalNumOfRxQueues() < p_dpdk_runtime_env_param->rx_queue_num) {

			string error_info = "Number of Required Rx Queue Exceeds What DPDK Device (" + dpdk_dev_i->getDeviceName() + ")" + " Can Support.";
			FATAL_ERROR(error_info);

		}

		if (dpdk_dev_i->getTotalNumOfTxQueues() < p_dpdk_runtime_env_param->tx_queue_num) {

			string error_info = "Number of Required Tx Queue Exceeds What DPDK Device (" + dpdk_dev_i->getDeviceName() + ")" + " Can Support.";
			FATAL_ERROR(error_info);

		} 

		// Enable Current DPDK Device
		if (dpdk_dev_i->openMultiQueues(p_dpdk_runtime_env_param->rx_queue_num, p_dpdk_runtime_env_param->tx_queue_num, dpdk_dev_cfg)) {

			LOGF("Successfully Enable DPDK Device (%s).", dpdk_dev_i->getDeviceName().c_str());

		} else {

			string error_info = "Fail to Enable DPDK Device (" + dpdk_dev_i->getDeviceName() + ")";
			FATAL_ERROR(error_info);

		}

	}

	return dpdk_dev_list;

}

dpdk_dev_map_list_t ConfigReaper::bind_rx_queue_to_cores(const vector<DpdkDevice *> dpdk_dev_list, const vector<SystemCore> & parser_cores) const {

	nic_queue_id_t total_used_queue_num = 0;
	
	// statistic all used queues from all DPDK Devices
	using nic_queue_list_t = vector<pair<nic_queue_id_t, DpdkDevice*> >;

	nic_queue_list_t queue_to_use;

	for (nic_queue_id_t i = 0; i < p_dpdk_runtime_env_param->rx_queue_num; i ++) {

		for (size_t j = 0; j < dpdk_dev_list.size(); j ++) {

			queue_to_use.push_back({i, dpdk_dev_list[j]});

		}

		total_used_queue_num += dpdk_dev_list.size();

	}

	nic_queue_id_t per_core_queue_num = total_used_queue_num / parser_cores.size();

	nic_queue_id_t remainder_queue_num = total_used_queue_num % parser_cores.size();
	
	// assert(per_core_queue_num * parser_cores.size() + remainder_queue_num == parser_cores.size());
	
	size_t queue_list_index = 0;

	dpdk_dev_map_list_t dpdk_dev_map_list;

	for (size_t i = 0; i < parser_cores.size(); i ++) {

		
		shared_ptr<DpdkDevMap> dpdk_dev_map_i = make_shared<DpdkDevMap>();

		if (dpdk_dev_map_i == nullptr) {

			FATAL_ERROR("Bad Memory Allocation for The Content of DPDK Devices Map.");

		}
		
		dpdk_dev_map_i->core_id = parser_cores[i].Id;

		for (nic_queue_id_t j = 0; j < per_core_queue_num; j ++) {

			if (queue_list_index == queue_to_use.size()) break;

			dpdk_dev_map_i->dpdk_dev_map[queue_to_use[queue_list_index].second].push_back(queue_to_use[queue_list_index].first);

			queue_list_index ++;
		}

		if (remainder_queue_num != 0) {

			dpdk_dev_map_i->dpdk_dev_map[queue_to_use[queue_list_index].second].push_back(queue_to_use[queue_list_index].first);

			queue_list_index ++;
			remainder_queue_num --;

		}

		dpdk_dev_map_list.push_back(dpdk_dev_map_i);
		
	}

	for (size_t i = 0; i < dpdk_dev_map_list.size(); i ++) {

		shared_ptr<DpdkDevMap> dpdk_dev_map_i = dpdk_dev_map_list[i];

		printf("Parser Core #%2d is Assigned with:\n", dpdk_dev_map_i->core_id);

		dpdk_dev_map_t::const_iterator j;

		size_t dpdk_dev_index = 0;

		for (j = dpdk_dev_map_i->dpdk_dev_map.cbegin(); j != dpdk_dev_map_i->dpdk_dev_map.cend(); j ++) {

			if (dpdk_dev_index ++) printf("\n");

			printf("\tDPDK Device(%s):", j->first->getDeviceName().c_str());

			for (size_t k = 0; k < j->second.size(); k ++) printf(" %d", j->second[k]);
			
		}

		printf("\n"); 
		
	}

	return dpdk_dev_map_list;

}

bool ConfigReaper::create_worker_threads(const dpdk_dev_map_list_t dpdk_dev_map_list, 
											vector<shared_ptr<ParserWorkerThread > > & parser_thread_vec, 
											vector<shared_ptr<AssemblerWorkerThread > > & assembler_thread_vec,
											vector<shared_ptr<InspectorWorkerThread > > & inspector_thread_vec,
											vector<shared_ptr<AggregatorWorkerThread > > & aggregator_thread_vec,
											vector<shared_ptr<DetectorWorkerThread > > & detector_thread_vec) {

	bool display_once = true;

	// dpdk_dev_map (a set of queues) -> parserWorkerThread
	// parserWorkerThread -> assemblerWorkerThread
	// assemblerWorkerThread -> detectorWorkerThread  

	for (cpu_core_id_t i = 0; i < p_dpdk_runtime_env_param->parser_cores_num; i ++) {

		const shared_ptr<ParserWorkerThread> p_parser_thread_i = make_shared<ParserWorkerThread>(dpdk_dev_map_list[i]);

		if (p_parser_thread_i == nullptr) {

			FATAL_ERROR("Bad Memory Allocation for Parser Thread.");

		}	

		parser_thread_vec.push_back(p_parser_thread_i);

		if (j_parser_params.size() != 0) p_parser_thread_i->load_params_via_json(j_parser_params);

		if (display_once) {

			p_parser_thread_i->p_parser_param->display_params();

			display_once = false;

		} 

	}

	size_t per_assembler_parser_num = parser_thread_vec.size() / p_dpdk_runtime_env_param->assembler_cores_num;
	size_t remainder_parser_num = parser_thread_vec.size() % p_dpdk_runtime_env_param->assembler_cores_num;

	size_t parser_thread_index = 0;

	display_once = true;
	
	for (cpu_core_id_t i = 0; i < p_dpdk_runtime_env_param->assembler_cores_num; i ++) {

		vector<shared_ptr<ParserWorkerThread> > parser_vec;

		for (size_t j = 0; j < per_assembler_parser_num; j ++) parser_vec.push_back(parser_thread_vec[parser_thread_index ++]);

		if (remainder_parser_num != 0) {

			parser_vec.push_back(parser_thread_vec[parser_thread_index ++]);

			remainder_parser_num --;

		}

		const shared_ptr<AssemblerWorkerThread > p_assembler_thread_i = make_shared<AssemblerWorkerThread >(parser_vec);

		if (p_assembler_thread_i == nullptr) {
		
			FATAL_ERROR("Bad Memory Allocation for Assembler Thread.");

		}
		
		if (j_assembler_params.size() != 0) {

			p_assembler_thread_i->load_params_via_json(j_assembler_params);

		}

		assembler_thread_vec.push_back(p_assembler_thread_i);

		if (display_once) {

			p_assembler_thread_i->p_assembler_param->display_params();

			display_once = false;

		} 

	} 


	size_t per_inspector_assembler_num = assembler_thread_vec.size() / p_dpdk_runtime_env_param->inspector_cores_num;
	size_t remainder_assembler_num = assembler_thread_vec.size() % p_dpdk_runtime_env_param->inspector_cores_num;

	size_t assembler_thread_index = 0;

	display_once = true;
	
	for (cpu_core_id_t i = 0; i < p_dpdk_runtime_env_param->inspector_cores_num; i ++) {

		vector<shared_ptr<AssemblerWorkerThread > > assembler_vec;

		for (size_t j = 0; j < per_inspector_assembler_num; j ++) assembler_vec.push_back(assembler_thread_vec[assembler_thread_index ++]);

		if (remainder_assembler_num != 0) {

			assembler_vec.push_back(assembler_thread_vec[assembler_thread_index ++]);

			remainder_assembler_num --;

		}

		const shared_ptr<InspectorWorkerThread > p_inspector_thread_i = make_shared<InspectorWorkerThread >(assembler_vec);

		if (p_inspector_thread_i == nullptr) {
		
			FATAL_ERROR("Bad Memory Allocation for Inspector Thread.");

		}
		
		if (j_inspector_params.size() != 0) {

			p_inspector_thread_i->load_params_via_json(j_inspector_params);

		}

		inspector_thread_vec.push_back(p_inspector_thread_i);

		if (display_once) {

			p_inspector_thread_i->p_inspector_param->display_params();

			display_once = false;

		} 

	} 

	display_once = true;

	for (cpu_core_id_t i = 0; i < p_dpdk_runtime_env_param->aggregator_cores_num; i ++) {

		const shared_ptr<AggregatorWorkerThread > p_aggregator_thread_i = make_shared<AggregatorWorkerThread >(inspector_thread_vec);

		if (p_aggregator_thread_i == nullptr) {

			FATAL_ERROR("Bad Memory Allocation for Aggregator Thread.");

		}	

		aggregator_thread_vec.push_back(p_aggregator_thread_i);

		if (j_aggregator_params.size() != 0) p_aggregator_thread_i->load_params_via_json(j_aggregator_params);

		if (display_once) {

			p_aggregator_thread_i->p_aggregator_param->display_params();

			display_once = false;

		} 

	}

	display_once = true;

	for (cpu_core_id_t i = 0; i < p_dpdk_runtime_env_param->detector_cores_num; i ++) {

		const shared_ptr<DetectorWorkerThread > p_detector_thread_i = make_shared<DetectorWorkerThread >(inspector_thread_vec, aggregator_thread_vec);

		if (p_detector_thread_i == nullptr) {

			FATAL_ERROR("Bad Memory Allocation for Detector Thread.");

		}	

		detector_thread_vec.push_back(p_detector_thread_i);

		if (j_detector_params.size() != 0) p_detector_thread_i->load_params_via_json(j_detector_params);

		if (display_once) {

			p_detector_thread_i->p_detector_param->display_params();

			display_once = false;

		} 

	}

	// // aggreagator num > inspector num
	// size_t per_inspector_aggregator_num = p_dpdk_runtime_env_param->aggregator_cores_num / inspector_thread_vec.size();
	// size_t remainder_aggregator_num = p_dpdk_runtime_env_param->aggregator_cores_num % inspector_thread_vec.size(); 

	// display_once = true;

	// for (cpu_core_id_t i = 0; i < p_dpdk_runtime_env_param->inspector_cores_num; i ++) {

	// 	for (size_t j = 0; j < per_inspector_aggregator_num; j ++) {

	// 		vector<shared_ptr<InspectorWorkerThread> > inspector_vec;

	// 		inspector_vec.push_back(inspector_thread_vec[i]);

	// 		const shared_ptr<AggregatorWorkerThread > p_aggregator_thread_i = make_shared<AggregatorWorkerThread >(inspector_vec);

	// 		if (p_aggregator_thread_i == nullptr) {
			
	// 			FATAL_ERROR("Bad Memory Allocation for Aggregator Thread.");

	// 		}
			
	// 		if (j_aggregator_params.size() != 0) {

	// 			p_aggregator_thread_i->load_params_via_json(j_aggregator_params);

	// 		}

	// 		aggregator_thread_vec.push_back(p_aggregator_thread_i);

	// 		if (display_once) {

	// 			p_aggregator_thread_i->p_aggregator_param->display_params();

	// 			display_once = false;

	// 		} 

	// 	}

	// 	if (remainder_aggregator_num != 0) {

	// 		vector<shared_ptr<InspectorWorkerThread> > inspector_vec;

	// 		inspector_vec.push_back(inspector_thread_vec[i]);

	// 		const shared_ptr<AggregatorWorkerThread > p_aggregator_thread_i = make_shared<AggregatorWorkerThread >(inspector_vec);

	// 		if (p_aggregator_thread_i == nullptr) {
			
	// 			FATAL_ERROR("Bad Memory Allocation for Aggregator Thread.");

	// 		}
			
	// 		if (j_aggregator_params.size() != 0) {

	// 			p_aggregator_thread_i->load_params_via_json(j_aggregator_params);

	// 		}

	// 		aggregator_thread_vec.push_back(p_aggregator_thread_i);

	// 		remainder_aggregator_num --;

	// 		if (display_once) {

	// 			p_aggregator_thread_i->p_aggregator_param->display_params();

	// 			display_once = false;

	// 		} 


	// 	}

	// }

	// size_t per_aggregator_inspector_num = inspector_thread_vec.size() / p_dpdk_runtime_env_param->aggregator_cores_num;
	// size_t remainder_inspector_num = inspector_thread_vec.size() % p_dpdk_runtime_env_param->aggregator_cores_num;

	// size_t inspector_thread_index = 0;

	// display_once = true;
	
	// for (cpu_core_id_t i = 0; i < p_dpdk_runtime_env_param->aggregator_cores_num; i ++) {

	// 	vector<shared_ptr<InspectorWorkerThread > > inspector_vec;

	// 	for (size_t j = 0; j < per_aggregator_inspector_num; j ++) inspector_vec.push_back(inspector_thread_vec[inspector_thread_index ++]);

	// 	if (remainder_inspector_num != 0) {

	// 		inspector_vec.push_back(inspector_thread_vec[assembler_thread_index ++]);

	// 		remainder_inspector_num --;

	// 	}

	// 	const shared_ptr<AggregatorWorkerThread > p_aggregator_thread_i = make_shared<AggregatorWorkerThread >(inspector_vec);

	// 	if (p_aggregator_thread_i == nullptr) {
		
	// 		FATAL_ERROR("Bad Memory Allocation for Aggregator Thread.");

	// 	}
		
	// 	if (j_aggregator_params.size() != 0) {

	// 		p_aggregator_thread_i->load_params_via_json(j_aggregator_params);

	// 	}

	// 	aggregator_thread_vec.push_back(p_aggregator_thread_i);

	// 	if (display_once) {

	// 		p_aggregator_thread_i->p_aggregator_param->display_params();

	// 		display_once = false;

	// 	} 

	// } 


	// size_t per_detector_inspector_num = inspector_thread_vec.size() / p_dpdk_runtime_env_param->detector_cores_num;
	// size_t remainder_inspector_num = inspector_thread_vec.size() % p_dpdk_runtime_env_param->detector_cores_num;

	// size_t per_detector_aggregator_num = aggregator_thread_vec.size() / p_dpdk_runtime_env_param->detector_cores_num;
	// remainder_aggregator_num = aggregator_thread_vec.size() % p_dpdk_runtime_env_param->detector_cores_num;

	// // size_t inspector_thread_index = 0;
	// size_t aggregator_thread_index = 0;

	// display_once = true;
	
	// for (cpu_core_id_t i = 0; i < p_dpdk_runtime_env_param->detector_cores_num; i ++) {

	// 	// vector<shared_ptr<InspectorWorkerThread> > inspector_vec;
	// 	vector<shared_ptr<AggregatorWorkerThread> > aggregator_vec;


	// 	// for (size_t j = 0; j < per_detector_inspector_num; j ++) inspector_vec.push_back(inspector_thread_vec[inspector_thread_index ++]);

	// 	// if (remainder_inspector_num != 0) {

	// 	// 	inspector_vec.push_back(inspector_thread_vec[inspector_thread_index ++]);

	// 	// 	remainder_inspector_num --;

	// 	// }

	// 	for (size_t j = 0; j < per_detector_aggregator_num; j ++) aggregator_vec.push_back(aggregator_thread_vec[aggregator_thread_index ++]);

	// 	if (remainder_aggregator_num != 0) {

	// 		aggregator_vec.push_back(aggregator_thread_vec[aggregator_thread_index ++]);

	// 		remainder_aggregator_num --;

	// 	}

	// 	const shared_ptr<DetectorWorkerThread > p_detector_thread_i = make_shared<DetectorWorkerThread >(aggregator_vec);

	// 	if (p_detector_thread_i == nullptr) {
		
	// 		FATAL_ERROR("Bad Memory Allocation for Detector Thread.");

	// 	}
		
	// 	if (j_detector_params.size() != 0) {

	// 		p_detector_thread_i->load_params_via_json(j_detector_params);

	// 	}

	// 	detector_thread_vec.push_back(p_detector_thread_i);

	// 	if (display_once) {

	// 		p_detector_thread_i->p_detector_param->display_params();

	// 		display_once = false;

	// 	} 

	// } 


	return true;

}

void ConfigReaper::interrupt_callback(void* cookie) {

	ThreadStateMonitor* monitor = (ThreadStateMonitor*) cookie;
	
	LOGF("DPDK Runtime Environment Clean Up...");

	DpdkDeviceList::getInstance().stopDpdkWorkerThreads();
	usleep(50000);

	double_t parsed_pkt_len = 0;

	bool print_parser_info = true;

	for (size_t i = 0; i < monitor->parser_worker_thread_vec.size(); i ++) {

		const pair<double_t, double_t> performance_i = monitor->parser_worker_thread_vec[i]->get_overall_performance();

		parsed_pkt_len += performance_i.second;

	}

	if (print_parser_info) {

		LOGF("Parser Overall Performance: [%4.4lf Gbps]", parsed_pkt_len);

	}

	// #ifndef START_PARSER_ONLY

	bool print_assembler_info = true;

	double_t update_pkt_len = 0;
	double_t fetch_pkt_len = 0;

	for (size_t i = 0; i < monitor->assembler_worker_thread_vec.size(); i ++) {

		const pair<double_t, double_t > performance_i = monitor->assembler_worker_thread_vec[i]->get_overall_performance();

		fetch_pkt_len += performance_i.first;
		update_pkt_len += performance_i.second;

	}

	if (print_assembler_info) {

		LOGF("Assembler Overall (Fetching) on Core #%d: [ %4.5lf Gbps ]", fetch_pkt_len);
        LOGF("Assembler Overall (Updateing) on Core #%d: [ %4.5lf Gbps ]", update_pkt_len);

	}

	bool print_aggregator_info = true;

	double_t create_pkt_len = 0;
	double_t aggr_pkt_len = 0;

	for (size_t i = 0; i < monitor->aggregator_worker_thread_vec.size(); i ++) {

		const pair<double_t, double_t > performance_i = monitor->aggregator_worker_thread_vec[i]->get_overall_performance();

		create_pkt_len += performance_i.first;
		aggr_pkt_len += performance_i.second;

	}

	if (print_aggregator_info) {

		LOGF("Aggregator (Creating) Overall Performance: [%4.4lf Gbps]", create_pkt_len);
		LOGF("Aggregator (Aggregating) Overall Performance: [%4.4lf Gbps]", aggr_pkt_len);

	}

	bool print_detector_info = true;

	double_t pre_pkt_len = 0;
	double_t inference_pkt_len = 0;

	ofstream f; 
	f.open("../" + monitor->detector_worker_thread_vec[0]->p_detector_param->model + "latency.txt", ios::out);

	for (size_t i = 0; i < monitor->detector_worker_thread_vec.size(); i ++) {

		for (const auto & _t : monitor->detector_worker_thread_vec[i]->inference_latency) {

			f << _t << endl;

		}

		const pair<double_t, double_t > performance_i = monitor->detector_worker_thread_vec[i]->get_overall_performance();

		pre_pkt_len += performance_i.first;
		inference_pkt_len += performance_i.second;

	}

	if (print_assembler_info) {

		LOGF("Detector (PreProcessing) Overall Performance: [%4.4lf Gbps]", pre_pkt_len);
		LOGF("Detector (Infernece) Overall Performance: [%4.4lf Gbps]", inference_pkt_len);

	}
	
	// #endif

	monitor->stop = true;

}

// modify the member named p_runtime_config_param
bool ConfigReaper::load_params_via_json(const json & jin) {

	if (p_dpdk_runtime_env_param) {

		p_dpdk_runtime_env_param = nullptr;
		
		return false;

	}

	try {
		
		p_dpdk_runtime_env_param = make_shared<DpdkRuntimeEnvParam>();

		if (jin.find("Parser") != jin.end()) {
			j_parser_params = jin["Parser"];
		} else {
			FATAL_ERROR("Parameters of Parser are Missing!");
		}

		if (jin.find("Assembler") != jin.end()) {
			j_assembler_params = jin["Assembler"];
		} else {
			FATAL_ERROR("Parameters of Assembler are Missing!");
		}

		if (jin.find("Inspector") != jin.end()) {
			j_inspector_params = jin["Inspector"];
		} else {
			FATAL_ERROR("Parameters of Inspector are Missing!");
		}

		if (jin.find("Aggregator") != jin.end()) {
			j_aggregator_params = jin["Aggregator"];
		} else {
			FATAL_ERROR("Parameters of Aggregator are Missing!");
		}

		if (jin.find("Detector") != jin.end()) {
			j_detector_params = jin["Detector"];
		} else {
			FATAL_ERROR("Parameters of Detector are Missing!");
		}


	} catch (exception & e) {

		FATAL_ERROR(e.what());
	
	}

	if (jin.find("DPDK") != jin.end()) {

		const json & dpdk_params = jin["DPDK"];

		// Dpdk Device
		// port vec
		if (dpdk_params.count("dpdk_port_vec")) {
			const vector<int> & port_vec = dpdk_params["dpdk_port_vec"];
			p_dpdk_runtime_env_param->dpdk_port_vec.clear();
			p_dpdk_runtime_env_param->dpdk_port_vec.assign(port_vec.cbegin(), port_vec.cend());
			p_dpdk_runtime_env_param->dpdk_port_vec.shrink_to_fit();
		} else {
			FATAL_ERROR("Parameter(dpdk_port_vec) is Missing!");
		}
		// rx/tx queue num
		if (dpdk_params.count("rx_queue_num")) {
			p_dpdk_runtime_env_param->rx_queue_num = static_cast<nic_queue_id_t>(dpdk_params["rx_queue_num"]);
		} else {
			FATAL_ERROR("Parameter(rx_queue_num) is Missing!");
		}

		if (dpdk_params.count("tx_queue_num")) {
			p_dpdk_runtime_env_param->tx_queue_num = static_cast<nic_queue_id_t>(dpdk_params["tx_queue_num"]);
		} else {
			FATAL_ERROR("Parameter(tx_queue_num) is Missing!");
		}

		p_dpdk_runtime_env_param->dpdk_cores_num = 1;

		// CPU core
		// Inspector cores
		if (dpdk_params.count("inspector_cores_num")) {
			p_dpdk_runtime_env_param->inspector_cores_num = static_cast<cpu_core_id_t>(dpdk_params["inspector_cores_num"]);
			p_dpdk_runtime_env_param->dpdk_cores_num += static_cast<cpu_core_id_t>(dpdk_params["inspector_cores_num"]);
		} else {
			FATAL_ERROR("Parameter(inspector_cores_num) is Missing!");
		}
		// Aggregator cores
		if (dpdk_params.count("aggregator_cores_num")) {
			p_dpdk_runtime_env_param->aggregator_cores_num = static_cast<cpu_core_id_t>(dpdk_params["aggregator_cores_num"]);
			p_dpdk_runtime_env_param->dpdk_cores_num += static_cast<cpu_core_id_t>(dpdk_params["aggregator_cores_num"]);
		} else {
			FATAL_ERROR("Parameter(aggregator_cores_num) is Missing!");
		}
		// Assembler cores
		if (dpdk_params.count("assembler_cores_num")) {
			p_dpdk_runtime_env_param->assembler_cores_num = static_cast<cpu_core_id_t>(dpdk_params["assembler_cores_num"]);
			p_dpdk_runtime_env_param->dpdk_cores_num += static_cast<cpu_core_id_t>(dpdk_params["assembler_cores_num"]);
		} else {
			FATAL_ERROR("Parameter(assembler_cores_num) is Missing!");
		}
		// parser cores
		if (dpdk_params.count("parser_cores_num")) {
			p_dpdk_runtime_env_param->parser_cores_num = static_cast<cpu_core_id_t>(dpdk_params["parser_cores_num"]);
			p_dpdk_runtime_env_param->dpdk_cores_num += static_cast<cpu_core_id_t>(dpdk_params["parser_cores_num"]);
		} else {
			FATAL_ERROR("Parameter(parser_cores_num) is Missing!");
		}
		// detector cores
		if (dpdk_params.count("detector_cores_num")) {
			p_dpdk_runtime_env_param->detector_cores_num = static_cast<cpu_core_id_t>(dpdk_params["detector_cores_num"]);
			p_dpdk_runtime_env_param->dpdk_cores_num += static_cast<cpu_core_id_t>(dpdk_params["detector_cores_num"]);
		} else {
			FATAL_ERROR("Parameter(parser_cores_num) is Missing!");
		}

		return true;

	} else {

		FATAL_ERROR("Parameters for DPDK Runtime Environment are Missing!");
	
	}

}

void ConfigReaper::enable_reaper() {

	LOGF("Check DPDK Runtime Enviroment...");

	// check parameters of DPDK Runtime Env

	// check whether parameters are loaded
	if (!p_dpdk_runtime_env_param) {

		FATAL_ERROR("Parameters of DPDK Runtime Environment are Not Found!");
	
	}

	// check existence of Dpdk devices 
	if (p_dpdk_runtime_env_param->dpdk_port_vec.empty()) {

		FATAL_ERROR("DPDK Devices Cannot be Found!");

	}

	// check whether cores of CPU are enough to configure dpdk runtime env
	CoreMask mask_all_machine_cores = getCoreMaskForAllMachineCores();
	vector<SystemCore> all_machine_cores;
	createCoreVectorFromCoreMask(mask_all_machine_cores, all_machine_cores);
	size_t all_machine_cores_num = all_machine_cores.size();
	
	if (all_machine_cores_num < p_dpdk_runtime_env_param->dpdk_cores_num) {

		FATAL_ERROR("DPDK Runtime Environment Param Error: Exceed Maximum Number of Cores in Current Machine!");

	}

	if (MAX_NUM_OF_CORES < p_dpdk_runtime_env_param->dpdk_cores_num) {

		FATAL_ERROR("DPDK Runtime Environment Param Error: Exceed Maximum Number of Cores PcapPlusPlus Supported!");

	}

	if (2 > p_dpdk_runtime_env_param->dpdk_cores_num) {

		FATAL_ERROR("DPDK Runtime Environment Param Error: Need at Least 2 Cores for Master Thread and One Parser Thread.");

	}

	// display parameters after checking
	p_dpdk_runtime_env_param->display_params();

	LOGF("Set Log Level...");	

	// Logger class is within namspace pcpp	
	Logger::getInstance().setAllModlesToLogLevel(Logger::Error);

	LOGF("Configure DPDK Runtime Environment Referring to Parameters...");
	
	CoreMask mask_dpdk_cores = (1 << p_dpdk_runtime_env_param->dpdk_cores_num) - 1;
	const vector<DpdkDevice *> dpdk_dev_list = configure_dpdk_runtime_env(mask_dpdk_cores);

	LOGF("Configure CPU Cores for DPDK Worker Threads...");

	// master Core index is 0	
	CoreMask mask_master_core = 1;
	CoreMask mask_without_master = mask_dpdk_cores & ~(mask_master_core);

	uint32_t shift_amount = p_dpdk_runtime_env_param->parser_cores_num + 1;
	CoreMask mask_parser_cores = mask_without_master & ((1 << shift_amount) - 1);
	CoreMask mask_without_parser_master = mask_dpdk_cores & ~(mask_parser_cores | mask_master_core);

	shift_amount += p_dpdk_runtime_env_param->assembler_cores_num;
	CoreMask mask_assembler_cores = mask_without_parser_master & ((1 << shift_amount) - 1);
	CoreMask mask_without_assembler_parser_master = mask_dpdk_cores & ~(mask_assembler_cores | mask_parser_cores | mask_master_core);

	shift_amount += p_dpdk_runtime_env_param->detector_cores_num;
	CoreMask mask_detector_cores = mask_without_assembler_parser_master & ((1 << shift_amount) - 1);
	CoreMask mask_without_detector_assembler_parser_master = mask_dpdk_cores & ~(mask_detector_cores | mask_assembler_cores | mask_parser_cores | mask_master_core);

	shift_amount += p_dpdk_runtime_env_param->inspector_cores_num;
	CoreMask mask_inspector_cores = mask_without_detector_assembler_parser_master & ((1 << shift_amount) - 1);
	CoreMask mask_without_inspector_detector_assembler_parser_master = mask_dpdk_cores & ~(mask_inspector_cores | mask_detector_cores | mask_assembler_cores | mask_parser_cores | mask_master_core);

	shift_amount += p_dpdk_runtime_env_param->aggregator_cores_num;
	CoreMask mask_aggregator_cores = mask_without_inspector_detector_assembler_parser_master & ((1 << shift_amount) - 1);

	vector<SystemCore> parser_cores, assembler_cores, inspector_cores, aggregator_cores, detector_cores;
	createCoreVectorFromCoreMask(mask_parser_cores, parser_cores);
	createCoreVectorFromCoreMask(mask_assembler_cores, assembler_cores);
	createCoreVectorFromCoreMask(mask_inspector_cores, inspector_cores);
	createCoreVectorFromCoreMask(mask_aggregator_cores, aggregator_cores);
	createCoreVectorFromCoreMask(mask_detector_cores, detector_cores);

	// cout << mask_parser_cores << ", " << mask_assembler_cores << ", " << mask_detector_cores << endl;

	// check whether each mask is correct
	assert((mask_parser_cores & mask_assembler_cores & mask_inspector_cores & mask_inspector_cores & mask_detector_cores) == 0);
	assert((mask_parser_cores | mask_assembler_cores | mask_inspector_cores | mask_aggregator_cores | mask_detector_cores) == mask_without_master);
	assert((mask_parser_cores | mask_assembler_cores | mask_inspector_cores | mask_aggregator_cores | mask_detector_cores | mask_master_core) == mask_dpdk_cores);

	dpdk_dev_map_list_t dpdk_dev_map_list = bind_rx_queue_to_cores(dpdk_dev_list, parser_cores);

	LOGF("Assign Rx Queues to Parser Cores...");

	vector<shared_ptr<ParserWorkerThread> > parser_thread_vec;
	vector<shared_ptr<AssemblerWorkerThread> > assembler_thread_vec;
	vector<shared_ptr<InspectorWorkerThread> > inspector_thread_vec;
	vector<shared_ptr<AggregatorWorkerThread> > aggregator_thread_vec;
	vector<shared_ptr<DetectorWorkerThread> > detector_thread_vec;

	if (!create_worker_threads(dpdk_dev_map_list, parser_thread_vec, assembler_thread_vec, inspector_thread_vec, aggregator_thread_vec, detector_thread_vec)) {

		FATAL_ERROR("Fail to Create DPDK Worker Threads! ");

	}

	LOGF("Start DPDK Worker Threads...");

	#ifdef SPLIT_START
		
		vector<DpdkWorkerThread*> thread_vec_all;
		
		transform(parser_thread_vec.cbegin(), parser_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<ParserWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 
		
		assert(parser_cores.size() == thread_vec_all.size());
		
		if (!DpdkDeviceList::getInstance().startDpdkWorkerThreads(mask_parser_cores, thread_vec_all)) {

			FATAL_ERROR("Fail to Start Parser Threads. ");

		}

		thread_vec_all.clear();
		
		transform(assembler_thread_vec.cbegin(), assembler_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<AssemblerWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 
		
		assert(assembler_cores.size() == thread_vec_all.size());
		
		if (!DpdkDeviceList::getInstance().startDpdkWorkerThreads(mask_assembler_cores, thread_vec_all)) {

			FATAL_ERROR("Fail to Start Assembler Threads. ");

		}

		thread_vec_all.clear();

		transform(inspector_thread_vec.cbegin(), inspector_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<InspectorWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 
		
		assert(inspector_cores.size() == thread_vec_all.size());
		
		if (!DpdkDeviceList::getInstance().startDpdkWorkerThreads(mask_inspector_cores, thread_vec_all)) {

			FATAL_ERROR("Fail to Start Inspector Threads. ");

		}

		thread_vec_all.clear();

		transform(aggregator_thread_vec.cbegin(), aggregator_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<AggregatorWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 
		
		assert(aggregator_cores.size() == thread_vec_all.size());
		
		if (!DpdkDeviceList::getInstance().startDpdkWorkerThreads(mask_aggregator_cores, thread_vec_all)) {

			FATAL_ERROR("Fail to Start Aggregator Threads. ");

		}

		thread_vec_all.clear();

		transform(detector_thread_vec.cbegin(), detector_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<DetectorWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 
		
		assert(detector_cores.size() == thread_vec_all.size());
		
		if (!DpdkDeviceList::getInstance().startDpdkWorkerThreads(mask_detector_cores, thread_vec_all)) {

			FATAL_ERROR("Fail to Start Detector Threads. ");

		}
	
	#else

		// #ifndef START_PARSER_ONLY

		vector<DpdkWorkerThread*> thread_vec_all;
		
		transform(parser_thread_vec.cbegin(), parser_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<ParserWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 
		
		assert(parser_cores.size() == thread_vec_all.size());

		transform(assembler_thread_vec.cbegin(), assembler_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<AssemblerWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 

		assert(parser_cores.size() + assembler_cores.size() == thread_vec_all.size());

		transform(inspector_thread_vec.cbegin(), inspector_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<InspectorWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 

		assert(parser_cores.size() + assembler_cores.size() + inspector_cores.size() == thread_vec_all.size());

		transform(aggregator_thread_vec.cbegin(), aggregator_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<AggregatorWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 

		assert(parser_cores.size() + assembler_cores.size() + inspector_cores.size() + aggregator_cores.size() == thread_vec_all.size());

		transform(detector_thread_vec.cbegin(), detector_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<DetectorWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 
		
		assert(parser_cores.size() + assembler_cores.size() + inspector_cores.size() + aggregator_cores.size() + detector_cores.size() == thread_vec_all.size());

		if (!DpdkDeviceList::getInstance().startDpdkWorkerThreads(mask_without_master, thread_vec_all)) {

			FATAL_ERROR("Fail to Start Worker Threads. ");

		}
		
		// #else 

		// 	vector<DpdkWorkerThread*> thread_vec_all;
		
		// 	transform(parser_thread_vec.cbegin(), parser_thread_vec.cend(), back_inserter(thread_vec_all), [] (shared_ptr<ParserWorkerThread> _p) -> DpdkWorkerThread* {return _p.get();}); 
			
		// 	assert(parser_cores.size() == thread_vec_all.size());
			
		// 	if (!DpdkDeviceList::getInstance().startDpdkWorkerThreads(mask_parser_cores, thread_vec_all)) {

		// 		FATAL_ERROR("Fail to Start Parser Threads. ");

		// 	}

		// #endif
	
	#endif

	// Monitor of worker threads, response to Interrupt
	ThreadStateMonitor monitor(parser_thread_vec, assembler_thread_vec, inspector_thread_vec, aggregator_thread_vec, detector_thread_vec);
	
	ApplicationEventHandler::getInstance().onApplicationInterrupted(interrupt_callback, &monitor);

	while(!monitor.stop) {

		multiPlatformSleep(5);

	}
 
}