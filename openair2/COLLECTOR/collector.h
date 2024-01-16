#ifndef COLLECTOR_H_
#define COLLECTOR_H_

#include "common/ran_context.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "COMMON/platform_types.h"
#include "collector_db_manager.h"
#include "collector_rest_manager.h"

#define COLLECTOR_UE_NUMBER 32
#define COLLECTOR_CC_NUMBER 3

typedef struct NR_mac_collector_UE_info {
    //identity parameters
    int rnti;
    int amf_ue_ngap_id;
    int ran_ue_ngap_id;
    uint8_t rrc_status;
    uint64_t current_tac;
    ImsiMobileIdentity_t _imsi;
    int mac_uid;
    uint8_t CC_id;
    //threshold
    float ul_thr_ue;
    float dl_thr_ue;
    uint32_t dl_current_rbs;
    uint32_t ul_current_rbs;
    uint32_t dl_total_rbs;
    uint32_t ul_total_rbs;
    uint64_t dl_total_bytes;
    uint32_t dl_current_bytes;
    uint64_t ul_total_bytes;
    uint32_t ul_current_bytes;
    uint64_t dl_lc_bytes[64];
    uint64_t ul_lc_bytes[64];
    //in order to differentiate
    uint32_t prev_dl_total_rbs;
    uint32_t prev_ul_total_rbs;
    uint64_t prev_dl_total_bytes;
    uint64_t prev_ul_total_bytes;
    uint64_t prev_dl_lc_bytes[64];
    uint64_t prev_ul_lc_bytes[64];    
} NR_mac_collector_UE_info_t;

typedef struct NR_mac_collector_CC_info {
    uint32_t                        dl_prb_total;
    uint32_t                        ul_prb_total;
    uint64_t                        dl_total_bytes;
    uint64_t                        ul_total_bytes;
    float                           dl_prb_usg;
    float                           ul_prb_usg;
    float                           dl_throughput;
    float                           ul_throughput;
    uint32_t                        dlBw;
    uint32_t                        ulBw;
    uint64_t                        dlCarrierFreq;
    uint8_t                         subcarrierSpacing;
    uint16_t                        slotsPassed;
    uint16_t                        timePassed;
    uint8_t                         carrierId;
    uint8_t                         num_connected_device;
    uint32_t                        dlFrequencyBand;
    //TODO: TAC is a temporary variable under CC. It must be move under RAN_CollectorStructure. (for db case it is written here.)
    uint64_t                        tracking_area_code;
} NR_mac_collector_CC_info_t;

typedef struct NR_mac_collector_struct {
    pthread_mutex_t                 mutex;
    NR_mac_collector_UE_info_t      *ueList[COLLECTOR_UE_NUMBER];
    NR_mac_collector_CC_info_t      *ccList[COLLECTOR_CC_NUMBER];
    pthread_t                       collector_thread;
} NR_mac_collector_struct_t;

#define Collector_UE_iterator(BaSe, VaR) NR_mac_collector_UE_info_t ** VaR##pptr=BaSe, *VaR; while ((VaR=*(VaR##pptr++)))
#define Collector_CELL_iterator(BaSe, VaR) NR_mac_collector_CC_info_t ** VaR##pptr=BaSe, *VaR; while ((VaR=*(VaR##pptr++)))

extern NR_mac_collector_struct_t macControllerStruct;
extern NR_mac_collector_struct_t macControllerStruct_prev;
void *NR_Collector_Task(void *arg);
void nr_collector_trigger(int CC_id, int frame, int subframe);

#endif 