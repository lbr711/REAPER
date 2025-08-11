#include "detectorWorker.hpp"

using namespace Reaper;

static inline double_t __get_double_ts() {

    struct timeval ts;
    gettimeofday(&ts, nullptr);
    return ts.tv_sec + ts.tv_usec*(1e-6);

}

bool DetectorWorkerThread::run(uint32_t coreId) {

	// 加载rnn模型
	long_model = torch::jit::load(p_detector_param->long_model_path);
	long_model.eval();

	aggr_model = torch::jit::load(p_detector_param->aggr_model_path);
	aggr_model.eval();

	LOGF("Detector on Core #%d Start", coreId);

	m_stop = false;
	m_core_id = coreId;

	double_t last_ts = __get_double_ts();

	while(!m_stop) {

		double_t curr_ts = __get_double_ts();
        double_t delta_time = (curr_ts - last_ts);

        if (delta_time > p_detector_param->report_interval) {

            if (p_detector_param->tracing_mode) {

            	double_t curr_inference_throughput, curr_pre_throughput;

            	if (sum_inference_pkt_len == 0) curr_inference_throughput = 0;
				else curr_inference_throughput = (((double_t) sum_inference_pkt_len) * 8.0) / inference_active_time / 1e9; 
                
				if (sum_pre_pkt_len == 0) curr_pre_throughput = 0;
                else curr_pre_throughput = (((double_t) sum_pre_pkt_len) * 8.0) / pre_active_time / 1e9; 

                LOGF("Detector Throughput on Core #%d: [ %4.5lf Gbps, %4.5lf Gbps ]", coreId, curr_pre_throughput, curr_inference_throughput);

            }

            last_ts = curr_ts;

        }

        for (size_t i = 0; i < p_aggregator_vec.size(); i ++) {

        	shared_ptr<PktMetaDataArrayOutput > curr_aggr_mts;

        	if (p_aggregator_vec[i]->p_short_aggr_queue->try_pop(curr_aggr_mts)) {

				if (curr_aggr_mts->first->size() >= 3 * p_detector_param->slice_len) {

					double_t pre_start_ts = __get_double_ts();
					torch::Tensor _ten;

					if (curr_aggr_mts->first->size() >= 3 * p_detector_param->trunc_flow_len) { 
						
						_ten = torch::from_blob(curr_aggr_mts->first->data(), {p_detector_param->trunc_flow_len, 3}, torch::kFloat64);
					
					} else {

						uint32_t _len = curr_aggr_mts->first->size() / 3;

						_ten = torch::from_blob(curr_aggr_mts->first->data(), {_len, 3}, torch::kFloat64);
				
					}

					torch::Tensor latter = _ten.index({torch::indexing::Slice(1), 0});
			        torch::Tensor former = _ten.index({torch::indexing::Slice(0, -1), 0});

		        	torch::Tensor intervals = latter - former;

		        	_ten.index_put_({torch::indexing::Slice(1), 0}, intervals);
		        	_ten.index_put_({0, 0}, 0);	

					torch::Tensor norm_ten = aggr_scale_ * _ten + aggr_min_;

					torch::Tensor slices = norm_ten.unfold(0, p_detector_param->slice_len, p_detector_param->stride).permute({0, 2, 1});

					aggr_inference_inputs.push_back(slices);
					double_t pre_end_ts = __get_double_ts();

					sum_pre_pkt_len += curr_aggr_mts->second;
					pre_active_time += (pre_end_ts - pre_start_ts); 

					double_t inference_start_ts = __get_double_ts();
					torch::jit::IValue res = aggr_model.forward(aggr_inference_inputs);
					double_t inference_end_ts = __get_double_ts();

					sum_inference_pkt_len += curr_aggr_mts->second;
					inference_active_time += (inference_end_ts - inference_start_ts); 
					inference_latency.push_back((inference_end_ts - inference_start_ts));

					// double kl_loss_i = res.toTensor().item<double_t >();

					aggr_inference_inputs.clear();

				} else {

					sum_inference_pkt_len += curr_aggr_mts->second;
					sum_pre_pkt_len += curr_aggr_mts->second;

				}

			}

        }

        for (size_t j = 0; j < p_inspector_vec.size(); j ++) {

        	shared_ptr<PktMetaDataArrayOutput > curr_long_mts;

			if (p_inspector_vec[j]->long_queue.try_pop(curr_long_mts)) {

				double_t pre_start_ts = __get_double_ts();
				torch::Tensor _ten;

				if (curr_long_mts->first->size() >= 3 * p_detector_param->trunc_flow_len) { 
					
					_ten = torch::from_blob(curr_long_mts->first->data(), {p_detector_param->trunc_flow_len, 3}, torch::kFloat64);
				
				} else {

					uint32_t _len = curr_long_mts->first->size() / 3;

					_ten = torch::from_blob(curr_long_mts->first->data(), {_len, 3}, torch::kFloat64);
			
				}

				torch::Tensor latter = _ten.index({torch::indexing::Slice(1), 0});
		        torch::Tensor former = _ten.index({torch::indexing::Slice(0, -1), 0});

	        	torch::Tensor intervals = latter - former;

	        	_ten.index_put_({torch::indexing::Slice(1), 0}, intervals);
	        	_ten.index_put_({0, 0}, 0);	

				torch::Tensor norm_ten = long_scale_ * _ten + long_min_;

				torch::Tensor slices = norm_ten.unfold(0, p_detector_param->slice_len, p_detector_param->stride).permute({0, 2, 1});

				long_inference_inputs.push_back(slices);
				double_t pre_end_ts = __get_double_ts();

				sum_pre_pkt_len += curr_long_mts->second;
				pre_active_time += (pre_end_ts - pre_start_ts); 

				double_t inference_start_ts = __get_double_ts();
				torch::jit::IValue res = long_model.forward(long_inference_inputs);
				double_t inference_end_ts = __get_double_ts();

				sum_inference_pkt_len += curr_long_mts->second;
				inference_active_time += (inference_end_ts - inference_start_ts); 	
				inference_latency.push_back((inference_end_ts - inference_start_ts));
		

				// double kl_loss_i = res.toTensor().item<double_t >();

				long_inference_inputs.clear();

			}


		}

	}

	return true;

}

void DetectorWorkerThread::stop() {

	LOGF("Detector on Core #%d Stop", m_core_id);
	
	m_stop = true;
	
}

pair<double_t, double_t > DetectorWorkerThread::get_overall_performance() const {

	if (!m_stop) {

		WARN("Detecting is Not Finished.");
		return {0.0, 0.0};
	
	}

	// 平均每条流的推断时间 mean_inference_time

	return {
        ((((double_t) sum_pre_pkt_len) * 8.0) /  (pre_active_time)) / 1e9,
        ((((double_t) sum_inference_pkt_len) * 8.0) /  (inference_active_time)) / 1e9
    };


}

void DetectorWorkerThread::load_params_via_json(const json &jin) {

	if (p_detector_param != nullptr) {

		WARN("Already Load Detector Parameters.");

	} 

	try {

		p_detector_param = make_shared<DetectorThreadParam>();

	} catch (exception & e) {

		FATAL_ERROR("Bad Memory Allocation for Detector Parameters.");

	}

	try {

		if (jin.count("report_interval")) {
			p_detector_param->report_interval = static_cast<decltype(p_detector_param->report_interval)>(jin["report_interval"]);
		} else {
			FATAL_ERROR("Parameter(report_interval) is Missing!");
		}

		if (jin.count("tracing_mode")) {
			p_detector_param->tracing_mode = jin["tracing_mode"];
		} else {
			FATAL_ERROR("Parameter(tracing_mode) is Missing!");

		}

		if (jin.count("slice_len")) {
			p_detector_param->slice_len = static_cast<decltype(p_detector_param->slice_len)>(jin["slice_len"]);
			p_detector_param->stride = p_detector_param->slice_len >> 2;
		} else {
			FATAL_ERROR("Parameter(slice_len) is Missing!");
		}

		if (jin.count("trunc_flow_len")) {
			p_detector_param->trunc_flow_len = static_cast<decltype(p_detector_param->trunc_flow_len)>(jin["trunc_flow_len"]);
		} else {
			FATAL_ERROR("Parameter(trunc_flow_len) is Missing!");
		}


		if (jin.count("model")) {
			p_detector_param->model = static_cast<decltype(p_detector_param->model)>(jin["model"]);

				p_detector_param->aggr_model_path = "../models4latency/" + p_detector_param->model + ".pt";
				p_detector_param->long_model_path = "../models4latency/" + p_detector_param->model + ".pt";

			// if (p_detector_param->model == "1001") {

			// 	p_detector_param->aggr_model_path = "../models/1001_aggr.pt";
			// 	p_detector_param->long_model_path = "../models/1001_long.pt";

			// 	// 1001 ******************************************************************************************************
			// 	// aggr
			// 	aggr_scale_ = torch::tensor({2.97384503e-08, 5.04719124e-05, 1.38765157e-06}, torch::kFloat64);
			// 	aggr_min_ = torch::tensor({0.0, -0.00141321, -0.54635728}, torch::kFloat64);
			// 	// long
			// 	long_scale_ = torch::tensor({5.54635692e-08, 4.27789185e-05, 1.38765157e-06}, torch::kFloat64);
			// 	long_min_ = torch::tensor({2.93956917e-06, -1.02669405e-03, -5.46357276e-01}, torch::kFloat64);
			// 	// ***********************************************************************************************************

			// } else if (p_detector_param->model == "1005") {

			// 	p_detector_param->aggr_model_path = "../models/1005_aggr.pt";
			// 	p_detector_param->long_model_path = "../models/1005_long.pt";

			// 	// 1005 ******************************************************************************************************
			// 	// aggr
			// 	aggr_scale_ = torch::tensor({3.26984386e-08, 5.74712644e-05, 1.38765157e-06}, torch::kFloat64);
			// 	aggr_min_ = torch::tensor({0.0, -0.0016092, -0.54635728}, torch::kFloat64);
			// 	// long
			// 	long_scale_ = torch::tensor({3.83084021e-08, 4.27880707e-05, 1.38765157e-06}, torch::kFloat64);
			// 	long_min_ = torch::tensor({1.30248567e-06, -1.24085405e-03, -5.46357276e-01}, torch::kFloat64);
			// 	// ***********************************************************************************************************

			// } else if (p_detector_param->model == "1009") {

			// 	p_detector_param->aggr_model_path = "../models/1009_aggr.pt";
			// 	p_detector_param->long_model_path = "../models/1009_long.pt";

			// 	// 1009 ******************************************************************************************************
			// 	// aggr
			// 	aggr_scale_ = torch::tensor({3.31864281e-08, 4.58715596e-05, 1.38765157e-06}, torch::kFloat64);
			// 	aggr_min_ = torch::tensor({0.0, -0.0012844, -0.54635728}, torch::kFloat64);
			// 	// long
			// 	long_scale_ = torch::tensor({5.95817705e-08, 4.27789185e-05, 1.38765157e-06}, torch::kFloat64);
			// 	long_min_ = torch::tensor({9.35433797e-06, -1.02669405e-03, -5.46357276e-01}, torch::kFloat64);
			// 	// ***********************************************************************************************************

			// } else if (p_detector_param->model == "1013") {

			// 	p_detector_param->aggr_model_path = "../models/1013_aggr.pt";
			// 	p_detector_param->long_model_path = "../models/1013_long.pt";

			// 	// 1013 ******************************************************************************************************
			// 	// aggr
			// 	aggr_scale_ = torch::tensor({3.63176780e-08, 5.74712644e-05, 1.38765157e-06}, torch::kFloat64);
			// 	aggr_min_ = torch::tensor({0.0, -0.0016092, -0.54635728}, torch::kFloat64);
			// 	// long
			// 	long_scale_ = torch::tensor({5.88198930e-08, 4.27862399e-05, 1.38715879e-06}, torch::kFloat64);
			// 	long_min_ = torch::tensor({2.99981454e-06, -1.19801472e-03, -5.45808145e-01}, torch::kFloat64);
			// 	// ***********************************************************************************************************

			// } else if (p_detector_param->model == "1017") {

			// 	p_detector_param->aggr_model_path = "../models/1017_aggr.pt";
			// 	p_detector_param->long_model_path = "../models/1017_long.pt";

			// 	// 1017 ******************************************************************************************************
			// 	// aggr
			// 	aggr_scale_ = torch::tensor({3.44240004e-08, 4.51997830e-05, 1.38765157e-06}, torch::kFloat64);
			// 	aggr_min_ = torch::tensor({0.0, -0.00126559, -0.54635728}, torch::kFloat64);
			// 	// long
			// 	long_scale_ = torch::tensor({5.92734287e-08, 4.27807487e-05, 1.38765157e-06}, torch::kFloat64);
			// 	long_min_ = torch::tensor({3.91204629e-06, -1.06951872e-03, -5.46357276e-01}, torch::kFloat64);
			// 	// ***********************************************************************************************************

			// } else if (p_detector_param->model == "1021") {

			// 	p_detector_param->aggr_model_path = "../models/1021_aggr.pt";
			// 	p_detector_param->long_model_path = "../models/1021_long.pt";

			// 	// 1021 ******************************************************************************************************
			// 	// aggr
			// 	aggr_scale_ = torch::tensor({3.28802436e-08, 4.27880707e-05, 1.38765157e-06}, torch::kFloat64);
			// 	aggr_min_ = torch::tensor({0.0, -0.00124085, -0.54635728}, torch::kFloat64);
			// 	// long
			// 	long_scale_ = torch::tensor({5.78491757e-08, 4.27789185e-05, 1.38765157e-06}, torch::kFloat64);
			// 	long_min_ = torch::tensor({1.79332445e-06, -1.02669405e-03, -5.46357276e-01}, torch::kFloat64);
			// 	// ***********************************************************************************************************

			// } else {

			// 	FATAL_ERROR("Parameter(model) is Incorrect!");

			// }

		} else {
			FATAL_ERROR("Parameter(model) is Missing!");
		}

	} catch (exception & e) {
		
		FATAL_ERROR(e.what());
		
	}

	return;

}