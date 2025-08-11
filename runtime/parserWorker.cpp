#include "parserWorker.hpp"

using namespace Reaper;

static inline double_t __get_double_ts() {

    struct timeval ts;
    gettimeofday(&ts, nullptr);
    return ts.tv_sec + ts.tv_usec*(1e-6);

}

bool ParserWorkerThread::run(uint32_t coreId) {

	if (p_dpdk_dev_map->dpdk_dev_map.size() == 0) {

		WARNF("No DPDK Devices Bound to Parser on Core #%2d", coreId);
		
		return false;

	}

	ring_queue_size = p_parser_param->pkt_meta_ring_queue_size;
	ring_queue_dropped = p_parser_param->pkt_meta_ring_queue_size / 4;

	ring_queue = shared_ptr<PacketMetaData[]>(new PacketMetaData[ring_queue_size](), std::default_delete<PacketMetaData[]>());
    
	if (ring_queue == nullptr) {
    
	    WARN("Bad Memory Allocation for Ring Queue.");
		
		return false;

    }

	// using p_mbuf_t = MBufRawPacket*; 
	// MBufRawPacket** arriving_pkts = new MBufRawPacket* [p_parser_param->burst_pkt_num];

	MBufRawPacket** arriving_pkts = (MBufRawPacket**) rte_malloc("arriving_pkts", sizeof(MBufRawPacket*) * p_parser_param->burst_pkt_num, 0);

	if (arriving_pkts == nullptr) {

		WARN("Bad Memory Allocation for Arriving Packets Buffer.");

		return false;
	}

	LOGF("Parser on Core #%d Start", coreId);

	m_stop = false;
	m_core_id = coreId;

	thread stat_tracer(&ParserWorkerThread::stat_tracer_exec, this);
	stat_tracer.detach();

	parser_start_time = __get_double_ts();

	while(!m_stop) {

		for (const auto & iter: p_dpdk_dev_map->dpdk_dev_map) {

			DpdkDevice* dpdk_dev = iter.first;

			for (const auto & queue_id: iter.second) {
				
				uint16_t recv_pkts_num = dpdk_dev->receivePackets(arriving_pkts, p_parser_param->burst_pkt_num, queue_id);
				// void MBufRawPacket::setMBuf(struct rte_mbuf* mBuf, timespec timestamp)
				// if (m_MBuf != nullptr && m_FreeMbuf) rte_pktmbuf_free(m_MBuf);
				// receivePacket在接受完一批新的MbufRawPacket前, 会释放上一批MbufRawPacket

				uint32_t src_ip;
				uint32_t dst_ip;
				uint32_t src_port;
				uint32_t dst_port;

				uint16_t pkt_length;
				uint64_t arr_time_stamp;
				uint32_t pkt_attr = 0x0;

				uint8_t proto;
				uint16_t flag_tcp;

				for (size_t i = 0; i < recv_pkts_num; i ++) {
					
					const uint16_t ether_type = ntohs(*(reinterpret_cast<const uint16_t* >(arriving_pkts[i]->getRawData() + 12)));

					arr_time_stamp = GET_UINT64_TS(arriving_pkts[i]->getPacketTimeStamp());

					// 只处理IPv4流量
					if (ether_type == 0x800) {

						const iphdr* ip_header = reinterpret_cast<const iphdr* >(arriving_pkts[i]->getRawData() + 14);

						proto = ip_header->protocol;

						// 只对具有五元组的flow进行统计
						// <symmetry<{SrcIP, SrcPort}, {DstIP, DstPort}>, proto >
						if (proto != 0x6 && proto != 0x11) continue;

						src_ip = ntohl(ip_header->ipSrc);
						dst_ip = ntohl(ip_header->ipDst);
						pkt_length = ntohs(ip_header->totalLength);

						pkt_attr = static_cast<uint32_t >(proto) << 16;

						const uint32_t l4LayerOffset = 14 + static_cast<uint32_t >(ip_header->internetHeaderLength) * 4;
						
						src_port = static_cast<uint32_t >(*(reinterpret_cast<const uint16_t* >(arriving_pkts[i]->getRawData() + l4LayerOffset)));
						dst_port = static_cast<uint32_t >(*(reinterpret_cast<const uint16_t* >(arriving_pkts[i]->getRawData() + l4LayerOffset + 2)));

						if (proto == 0x6) {

							flag_tcp = static_cast<uint16_t >((*(reinterpret_cast<const uint16_t* >(arriving_pkts[i]->getRawData() + l4LayerOffset + 13))) & 0x3f);
							pkt_attr = pkt_attr | (flag_tcp << 8);

						}

						dpdk_dev_parsed_pkt_len[dpdk_dev->getDeviceId()] += pkt_length;
						dpdk_dev_parsed_pkt_num[dpdk_dev->getDeviceId()] ++;


					} else continue;

					shared_ptr<PacketMetaData > p_meta = make_shared<PacketMetaData >(src_ip, dst_ip, src_port, dst_port, proto, pkt_attr, pkt_length, arr_time_stamp);

					if (p_meta == nullptr) continue;

					// the enqueue operation of ring queue is only performed by its parser. 
					assert(ring_queue_count != ring_queue_size);
					
					acquire_semaphore();
					// enqueue the element at ring_queue_end 
					ring_queue[ring_queue_end] = *p_meta;
					ring_queue_count ++;
					// update ring_queue_end
					ring_queue_end = (ring_queue_end + 1) % ring_queue_size;
					release_semaphore();

					// the ring queue reach its max (ring_queue_begin == ring_queue_end)
					if (ring_queue_count == ring_queue_size) {
						
						WARNF("Parser on core # %2d: parse ring queue reach max.", (int)this->getCoreId());
						acquire_semaphore();
						// release ring queue space
						// dequeue 1/4 elements from ring queue.
						ring_queue_begin = (ring_queue_begin + ring_queue_dropped) % ring_queue_size;
						ring_queue_count -= ring_queue_dropped;
						release_semaphore();

					}

				} 

			}

		}



	}

	for (size_t i = 0; i < p_parser_param->burst_pkt_num; i ++) {

		if (arriving_pkts[i] != nullptr) {
			delete arriving_pkts[i];
		}

	}

	rte_free(arriving_pkts);

	return true;

}

void ParserWorkerThread::stop() {

	LOGF("Parser on Core #%d Stop", m_core_id);
	
	m_stop = true;
	parser_end_time = __get_double_ts();

	// final tracing report
	if (p_parser_param->tracing_mode) {

		stringstream ss;
			
		ss << "Parser on Core #" << setw(2) << m_core_id << ": ";

		for (dpdk_dev_map_t::const_iterator ite = p_dpdk_dev_map->dpdk_dev_map.cbegin(); 
			ite != p_dpdk_dev_map->dpdk_dev_map.cend(); ite ++) {

				nic_port_id_t dpdk_dev_port = ite->first->getDeviceId();
				ss << "DPDK Port" << setw(2) << dpdk_dev_port;
				ss << "[" << setw(5) << setprecision(3) << ((double) dpdk_dev_parsed_pkt_num[dpdk_dev_port] / 1e6) / p_parser_param->report_interval << " Mpps / ";
				ss << setw(5) << setprecision(3) << ((double) dpdk_dev_parsed_pkt_len[dpdk_dev_port] / (1e9 / 8)) / p_parser_param->report_interval << " Gbps]\t";

				dpdk_dev_sum_parsed_pkt_len[dpdk_dev_port] += dpdk_dev_parsed_pkt_len[dpdk_dev_port];
				dpdk_dev_sum_parsed_pkt_num[dpdk_dev_port] += dpdk_dev_parsed_pkt_num[dpdk_dev_port];

				dpdk_dev_parsed_pkt_len[dpdk_dev_port] = 0;
				dpdk_dev_parsed_pkt_num[dpdk_dev_port] = 0;
				
		}

		ss << endl;
		printf("%s", ss.str().c_str());

	} else {

		for (dpdk_dev_map_t::const_iterator ite = p_dpdk_dev_map->dpdk_dev_map.cbegin(); 
			ite != p_dpdk_dev_map->dpdk_dev_map.cend(); ite ++) {

				nic_port_id_t dpdk_dev_port = ite->first->getDeviceId();

				dpdk_dev_sum_parsed_pkt_len[dpdk_dev_port] += dpdk_dev_parsed_pkt_len[dpdk_dev_port];
				dpdk_dev_sum_parsed_pkt_num[dpdk_dev_port] += dpdk_dev_parsed_pkt_num[dpdk_dev_port];

				dpdk_dev_parsed_pkt_len[dpdk_dev_port] = 0;
				dpdk_dev_parsed_pkt_num[dpdk_dev_port] = 0;
				
		}

	}

}

void ParserWorkerThread::stat_tracer_exec() const {

	while (!m_stop) {

		if (p_parser_param->tracing_mode) {

			stringstream ss;
			
			ss << "Parser on Core #" << setw(2) << m_core_id << ": ";

			for (dpdk_dev_map_t::const_iterator ite = p_dpdk_dev_map->dpdk_dev_map.cbegin(); 
				ite != p_dpdk_dev_map->dpdk_dev_map.cend(); ite ++) {

					nic_port_id_t dpdk_dev_port = ite->first->getDeviceId();
					ss << "DPDK Port" << setw(2) << dpdk_dev_port;
					ss << " [" << setw(5) << setprecision(3) << ((double) dpdk_dev_parsed_pkt_num[dpdk_dev_port] / 1e6) / p_parser_param->report_interval << " Mpps / ";
					ss << setw(5) << setprecision(3) << ((double) dpdk_dev_parsed_pkt_len[dpdk_dev_port] / (1e9 / 8)) / p_parser_param->report_interval << " Gbps]\t";

					// LOGF("parser on Core #%d: [ %4.5lf Mpps / %4.5lf Gbps ]",  m_core_id, ((double) dpdk_dev_parsed_pkt_num[dpdk_dev_port] / 1e6) / p_parser_param->report_interval, 
					// ((double) dpdk_dev_parsed_pkt_len[dpdk_dev_port] / (1e9 / 8)) / p_parser_param->report_interval);

					dpdk_dev_sum_parsed_pkt_len[dpdk_dev_port] += dpdk_dev_parsed_pkt_len[dpdk_dev_port];
					dpdk_dev_sum_parsed_pkt_num[dpdk_dev_port] += dpdk_dev_parsed_pkt_num[dpdk_dev_port];

					dpdk_dev_parsed_pkt_len[dpdk_dev_port] = 0;
					dpdk_dev_parsed_pkt_num[dpdk_dev_port] = 0;
				
			}

			ss << endl;
			printf("%s", ss.str().c_str());

		} else {

			for (dpdk_dev_map_t::const_iterator ite = p_dpdk_dev_map->dpdk_dev_map.cbegin(); 
				ite != p_dpdk_dev_map->dpdk_dev_map.cend(); ite ++) {

					nic_port_id_t dpdk_dev_port = ite->first->getDeviceId();

					dpdk_dev_sum_parsed_pkt_len[dpdk_dev_port] += dpdk_dev_parsed_pkt_len[dpdk_dev_port];
					dpdk_dev_sum_parsed_pkt_num[dpdk_dev_port] += dpdk_dev_parsed_pkt_num[dpdk_dev_port];

					dpdk_dev_parsed_pkt_len[dpdk_dev_port] = 0;
					dpdk_dev_parsed_pkt_num[dpdk_dev_port] = 0;
				
			}

		}

		sleep(p_parser_param->report_interval);

	}

}

pair<double_t, double_t> ParserWorkerThread::get_overall_performance() const {

	if (!m_stop) {

		WARN("Parsing is Not Finished.");
		return {0, 0};
	
	}

	double_t overall_parsed_pkt_num_speed = 0, overall_parsed_pkt_len_speed = 0;

	for (dpdk_dev_map_t::const_iterator ite = p_dpdk_dev_map->dpdk_dev_map.cbegin();
		ite != p_dpdk_dev_map->dpdk_dev_map.cend(); ite ++) {

		nic_port_id_t dpdk_dev_port = ite->first->getDeviceId();
		
		double_t dpdk_dev_parsed_pkt_num_speed = ((double) dpdk_dev_sum_parsed_pkt_num[dpdk_dev_port] / 1e6) / (parser_end_time - parser_start_time);
		double_t dpdk_dev_parsed_pkt_len_speed = ((double) dpdk_dev_sum_parsed_pkt_len[dpdk_dev_port] / (1e9 / 8)) / (parser_end_time - parser_start_time);

		overall_parsed_pkt_num_speed += dpdk_dev_parsed_pkt_num_speed;
		overall_parsed_pkt_len_speed += dpdk_dev_parsed_pkt_len_speed;

	}

	return {overall_parsed_pkt_num_speed, overall_parsed_pkt_len_speed};

}

void ParserWorkerThread::load_params_via_json(const json &jin) {

	if (p_parser_param != nullptr) {

		WARN("Already Load Parser Parameters.");

	} 

	try {

		p_parser_param = make_shared<ParserThreadParam>();

	} catch (exception & e) {

		FATAL_ERROR("Bad Memory Allocation for Parser Parameters.");

	}

	try {

		if (jin.count("report_interval")) {
			p_parser_param->report_interval = static_cast<decltype(p_parser_param->report_interval)>(jin["report_interval"]);
		} else {
			FATAL_ERROR("Parameter(report_interval) is Missing!");
		}

		if (jin.count("pkt_meta_ring_queue_size")) {
			p_parser_param->pkt_meta_ring_queue_size = static_cast<decltype(p_parser_param->pkt_meta_ring_queue_size)>(jin["pkt_meta_ring_queue_size"]);
			if (p_parser_param->pkt_meta_ring_queue_size > MAX_PKT_META_RING_QUEUE_SIZE) {
				FATAL_ERROR("Exceed Max Ring Queue Size.");
			}
		} else {
			FATAL_ERROR("Parameter(pkt_meta_ring_queue_size) is Missing!");
		}

		if (jin.count("burst_pkt_num")) {
			p_parser_param->burst_pkt_num = static_cast<decltype(p_parser_param->burst_pkt_num)>(jin["burst_pkt_num"]);
			if (p_parser_param->burst_pkt_num > MAX_BURST_PKT_NUM) {
				FATAL_ERROR("Exceed Max Number of Burst Packet.");
			}
		} else {
			FATAL_ERROR("Parameter(burst_pkt_num) is Missing!");
		}

		if (jin.count("tracing_mode")) {
			p_parser_param->tracing_mode = jin["tracing_mode"];
		} else {
			FATAL_ERROR("Parameter(tracing_mode) is Missing!");

		}

	} catch (exception & e) {
		
		FATAL_ERROR(e.what());
		
	}

	return;

}