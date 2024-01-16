#include "collector.h"
#include "openair2/COMMON/platform_types.h"
#include "common/utils/LOG/log.h"
#include "intertask_interface.h"
#include "LAYER2/NR_MAC_COMMON/nr_mac_extern.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "GNB_APP/gnb_config.h"
#include <time.h>
#include "COMMON/rrc_messages_types.h"
#include <sqlite3.h>
#include <hiredis/hiredis.h>
#include "RRC/NR/rrc_gNB_UE_context.h"
#include "PHY/defs_gNB.h"
#include "LAYER2/nr_pdcp/nr_pdcp.h"
#include "RRC/NR/nr_rrc_defs.h"


#define BYTES_TO_KB(bytes) ((bytes) / 1024.0)
#define MSEC_TO_SEC(miliseconds) ((miliseconds) / 1000.0)

typedef enum timer_opt{
    LinuxTimerBased = 0,
    SubframeBased = 1
} StatsCollectionOption;

extern void prepare_scc(NR_ServingCellConfigCommon_t *scc);
extern RAN_CONTEXT_t RC;

const int moduleP = 0;
const int CC_id = 0;
const int copyStatsOption = 0;
uint32_t collectionInterval = 1000; //miliseconds
#define frameInterval ((int) (collectionInterval / 10))
const StatsCollectionOption collectionOpt = SubframeBased;
// const StatsCollectionOption collectionOpt = LinuxTimerBased;

bool firstTimeCopyFlag = false;
bool intervalChangedFlag = false;
NR_mac_collector_struct_t macControllerStruct = {0};
NR_mac_collector_struct_t macControllerStruct_prev = {0};




sqlite3* db;
redisContext* redis;

int countSetBits(uint64_t n) {
    int count = 0;
    while(n != 0) {
        n = n & (n - 1); // clear least significant 1-bit
        count++;
    }
    return count;
}


bool is_rnti_matched(const void* ueP, rnti_t rnti){
    const NR_mac_collector_UE_info_t * ue = (const NR_mac_collector_UE_info_t *) ueP;
    return (ue->rnti == (int)rnti);
}

NR_mac_collector_UE_info_t * find_ue_by_rnti(NR_mac_collector_struct_t* collectorStruct, rnti_t rnti)
{
    Collector_UE_iterator(collectorStruct->ueList, UE){
        if (is_rnti_matched(UE, rnti)) {
            return UE;
        }
    }
    return NULL;
}

void set_ues_mac_statistics(NR_mac_collector_UE_info_t *UE_c, NR_mac_stats_t *stats, NR_mac_collector_UE_info_t *UE_prev)
{
    UE_c->dl_total_rbs = stats->dl.total_rbs - UE_prev->prev_dl_total_rbs;
    UE_c->ul_total_rbs = stats->ul.total_rbs - UE_prev->prev_ul_total_rbs;
    UE_c->dl_total_bytes = stats->dl.total_bytes - UE_prev->prev_dl_total_bytes;
    UE_c->ul_total_bytes = stats->ul.total_bytes - UE_prev->prev_ul_total_bytes;
    UE_c->dl_lc_bytes[4] = stats->dl.lc_bytes[4] - UE_prev->prev_dl_lc_bytes[4];
    UE_c->ul_lc_bytes[4] = stats->ul.lc_bytes[4] - UE_prev->prev_ul_lc_bytes[4];
}

void set_ues_initial_mac_statistics(NR_mac_collector_UE_info_t *UE_c, NR_mac_stats_t *stats)
{
    UE_c->dl_total_rbs = stats->dl.total_rbs;
    UE_c->ul_total_rbs = stats->ul.total_rbs;
    UE_c->dl_total_bytes = stats->dl.total_bytes;
    UE_c->ul_total_bytes = stats->ul.total_bytes;
    UE_c->dl_lc_bytes[4] = stats->dl.lc_bytes[4];
    UE_c->ul_lc_bytes[4] = stats->ul.lc_bytes[4];
}

void set_ues_previous_mac_statistics(NR_mac_collector_UE_info_t *UE_c, NR_mac_stats_t *stats)
{
    UE_c->prev_dl_total_rbs = stats->dl.total_rbs;
    UE_c->prev_ul_total_rbs = stats->ul.total_rbs;
    UE_c->prev_dl_total_bytes = stats->dl.total_bytes;
    UE_c->prev_ul_total_bytes = stats->ul.total_bytes;
    UE_c->prev_dl_lc_bytes[4] = stats->dl.lc_bytes[4];
    UE_c->prev_ul_lc_bytes[4] = stats->ul.lc_bytes[4];
}

void reset_mac_collector(NR_mac_collector_struct_t* macCollector)
{
    for (uint8_t i = 0; i < COLLECTOR_CC_NUMBER; i++){
        if (macCollector->ccList[i] != NULL) {
            free(macCollector->ccList[i]);
            macCollector->ccList[i] = NULL;
        }
    }
    for (uint8_t i = 0; i < COLLECTOR_UE_NUMBER; i++){
        if (macCollector->ueList[i] != NULL) {
            free(macCollector->ueList[i]);
            macCollector->ueList[i] = NULL;
        }
    }
}

void copy_mac_stats(gNB_MAC_INST * gNB, NR_mac_collector_struct_t * collectorStruct, uint8_t CC_id) {
    //collectionInterval / 10 = number of frames.
    //calculation of number of prbs in given time interval.
    //act like single cell
    const uint64_t current_tracking_are_code = RC.nrrrc[moduleP]->configuration.tac;
    // -- configuration variables --
    const PHY_VARS_gNB* const phyInst = RC.gNB[moduleP];
    const int DlPrbsInSlot = phyInst->frame_parms.N_RB_DL;
    const int UlPrbsInSlot = phyInst->frame_parms.N_RB_UL;
    const int tempScs = phyInst->frame_parms.subcarrier_spacing;
    const uint64_t dlCarrierFreq = phyInst->frame_parms.dl_CarrierFreq;
    const int slotsPerSubFrame = phyInst->frame_parms.slots_per_subframe;

    const nfapi_nr_config_request_scf_t * const cfg = &gNB->config[CC_id];
    const uint16_t dlBandwidth = cfg->carrier_config.dl_bandwidth.value;
    const uint16_t ulBandwidth = cfg->carrier_config.uplink_bandwidth.value;

    const uint32_t slotsInCollectionInterval = slotsPerSubFrame * 10 * (collectionInterval/10);

    const float dlPrbs = DlPrbsInSlot * slotsInCollectionInterval;
    const float ulPrbs = UlPrbsInSlot * slotsInCollectionInterval;
    uint8_t dlSlotsInSubframe = countSetBits(gNB->dlsch_slot_bitmap[0]);
    uint8_t ulSlotsInSubframe = countSetBits(gNB->ulsch_slot_bitmap[0]);

    float dlPrbsInTotal   = dlPrbs * (float)dlSlotsInSubframe/(slotsPerSubFrame*10);
    float ulPrbsInTotal   = ulPrbs * (float)ulSlotsInSubframe/(slotsPerSubFrame*10);

    LOG_D(NR_RRC, "\ndlSlotsInSubframe: %u ulSlotsInSubframe: %u \ndlPrbsInTotal: %f ulPrbsInTotal: %f\n",dlSlotsInSubframe,ulSlotsInSubframe,dlPrbsInTotal,ulPrbsInTotal);

    // -- collecting cell level statistics --
    NR_mac_collector_CC_info_t* CC_c = (NR_mac_collector_CC_info_t*)calloc(1, sizeof(NR_mac_collector_CC_info_t));
    collectorStruct->ccList[CC_id] = CC_c;
    CC_c->carrierId = CC_id;
    CC_c->slotsPassed = slotsInCollectionInterval;
    CC_c->timePassed = collectionInterval;
    CC_c->ulBw = ulBandwidth;
    CC_c->dlBw = dlBandwidth;
    CC_c->dlCarrierFreq = dlCarrierFreq;
    CC_c->subcarrierSpacing = (uint8_t)(tempScs / 1000);
    
    pthread_mutex_lock(&gNB->UE_info.mutex);
    int ueNumber = 0;
    UE_iterator(gNB->UE_info.list, UE) {
        NR_mac_stats_t *stats = &UE->mac_stats;
        NR_mac_collector_UE_info_t *UE_c = (NR_mac_collector_UE_info_t *)calloc(1,sizeof(NR_mac_collector_UE_info_t));
        UE_c->rnti = UE->rnti;
        UE_c->mac_uid = UE->uid;
        UE_c->CC_id = CC_id; //UE->CellGroup->cellGroupId;
        UE_c->current_tac = current_tracking_are_code;

        rrc_gNB_ue_context_t *ue_context;
        ue_context = rrc_gNB_get_ue_context_by_rnti(RC.nrrrc[moduleP], UE_c->rnti);
        if (ue_context == NULL) {
            LOG_D(NR_RRC, "\nCOLLECTOR: This UE with rnti: %d not included in RRC Context!\n", UE_c->rnti);
        } else {
            UE_c->amf_ue_ngap_id = ue_context->ue_context.amf_ue_ngap_id;
            UE_c->ran_ue_ngap_id = ue_context->ue_context.rrc_ue_id;
            UE_c->rrc_status = (uint8_t)ue_context->ue_context.StatusRrc;

            if (ue_context->ue_context.StatusRrc == NR_RRC_CONNECTED && ue_context->ue_context.nb_of_pdusessions > 0) {
                CC_c->num_connected_device++;
            }
        }
        
        collectorStruct->ueList[ueNumber] = UE_c;
        NR_mac_collector_UE_info_t *UE_prev = find_ue_by_rnti(&macControllerStruct_prev, UE_c->rnti);

        if (!firstTimeCopyFlag && UE_prev != NULL) {
            set_ues_mac_statistics(UE_c, stats, UE_prev);
        } else {
            set_ues_initial_mac_statistics(UE_c, stats);
        }
        set_ues_previous_mac_statistics(UE_c, stats);


        UE_c->dl_thr_ue = UE->dl_thr_ue;
        UE_c->ul_thr_ue = UE->ul_thr_ue;

        //Carrier Statistics.
        CC_c->dl_prb_total += UE_c->dl_total_rbs;
        CC_c->ul_prb_total += UE_c->ul_total_rbs;
        CC_c->dl_total_bytes += UE_c->dl_total_bytes;
        CC_c->ul_total_bytes += UE_c->ul_total_bytes;
        LOG_D(NR_RRC, "\nStatistics for ue: %d \n dl_total_rbs: %d \t ul_total_rbs: %d\n",
        ueNumber, UE_c->dl_total_rbs, UE_c->ul_total_rbs);
        ueNumber++;
        
    }
    pthread_mutex_unlock(&gNB->UE_info.mutex);
    LOG_D(NR_RRC, "\nStatistics for cell: \n dl_total_rbs: %d \t ul_total_rbs: %d\n", CC_c->dl_prb_total, CC_c->ul_prb_total);
    float dlUsage = CC_c->dl_prb_total / dlPrbsInTotal;
    float ulUsage = CC_c->ul_prb_total / ulPrbsInTotal;
    if ((dlUsage > 1.0)){
        LOG_E(NR_RRC, "\nDL Resource Usage Error: dlUsage: %f\n", dlUsage);
        dlUsage = 1.0;
    }

    if ((ulUsage > 1.0)){
        LOG_E(NR_RRC, "\nUL Resource Usage Error: ulUsage: %f\n", ulUsage);
        ulUsage = 1.0;
    }

    CC_c->dl_prb_usg = dlUsage;
    CC_c->ul_prb_usg = ulUsage;
    LOG_D(NR_RRC, "\nCOLLECTOR USAGE: \n dl: %f \t ul: %f\n", CC_c->dl_prb_usg, CC_c->ul_prb_usg);
    CC_c->ul_throughput = BYTES_TO_KB(CC_c->ul_total_bytes) / MSEC_TO_SEC(collectionInterval);             //kbps
    CC_c->dl_throughput = BYTES_TO_KB(CC_c->dl_total_bytes) / MSEC_TO_SEC(collectionInterval);             //kbps
    LOG_D(NR_RRC, "Dl throughput is : %f \n Collection Interval is %f seconds\n", CC_c->dl_throughput, (float)collectionInterval/1000);
    CC_c->dlFrequencyBand = *(RC.nrrrc[moduleP]->configuration.scc->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.array[0]);
    CC_c->tracking_area_code = current_tracking_are_code;

    firstTimeCopyFlag = false;
    reset_mac_collector(&macControllerStruct_prev);
}

//OPTION 1: copy MAC, RLC, PDCP stats before writing to database
void copy_stats(uint8_t CC_id)
{
    // LOG_W(NR_RRC, "COLLECTOR: copies stats right now for CC_id : %u \n", CC_id);
    copy_mac_stats(RC.nrmac[0], &macControllerStruct, CC_id);
}

//OPTION 2 : copy MAC, RLC, PDCP stats in another thred.
void *nr_collector_copy_struct_thread(void *arg) {

  NR_mac_collector_struct_t *collectorNB = (NR_mac_collector_struct_t *)arg;

  while (oai_exit == 0) {
    copy_mac_stats(RC.nrmac[0], collectorNB, 0);
    usleep(1000*1000);
  }

  return NULL;
}

void nr_collector_handle_subframe_process(int frame, int subframe, int CC_id){
    LOG_D(NR_RRC, "WRITE DB TASK HAS CAME : frame: %d subframe: %d !\n", frame, subframe);
    copy_stats(CC_id);
    insert_nr_mac_connector_struct_to_redis_database(&redis, &macControllerStruct);
    reset_mac_collector(&macControllerStruct);
}

void *NR_Collector_Task(void *arg) {
    MessageDef *received_msg = NULL;
    int         result;
    LOG_I(NR_RRC, "Starting Collector Task!");
    itti_mark_task_ready(TASK_COLLECTOR);

    sleep(2);

    // create_connector_database(&db);

    int ret = create_connector_redis_database(&redis, "127.0.0.1", 6379);
    if (ret == 0)
    {
        LOG_I(NR_RRC, "COLLECTOR:  REDIS DATABASE CREATED SUCCESSFULLY! \n");
    }
    
    long timer_id;
    if (collectionOpt == LinuxTimerBased){
        timer_setup((int)(collectionInterval/1000), 0, TASK_COLLECTOR, 0, TIMER_PERIODIC, NULL, &timer_id);
    }
    
    if (copyStatsOption == 1){
        threadCreate(&macControllerStruct.collector_thread, nr_collector_copy_struct_thread, (void*)&macControllerStruct, "COLLECTOR_STATS", -1,     sched_get_priority_min(SCHED_OAI)+1 );
    }
    collector_rest_listener_args_t listenerArgs;
    listenerArgs.timeInterval = &collectionInterval;
    listenerArgs.firstTimeCopyFlag = &firstTimeCopyFlag;
    listenerArgs.intervalChangedFlag = &intervalChangedFlag;
    threadCreate(&listenerArgs.collector_listener_thread, nr_collector_rest_listener, (void*)&listenerArgs, "COLLECTOR_LISTENER", -1, sched_get_priority_min(SCHED_OAI)+1);
    
       
    while (1) {
        itti_receive_msg(TASK_COLLECTOR, &received_msg);
        LOG_D(NR_RRC, "Collector Task Received %s for instance %ld\n", ITTI_MSG_NAME(received_msg), ITTI_MSG_DESTINATION_INSTANCE(received_msg));
        switch (ITTI_MSG_ID(received_msg)) {

            case TIMER_HAS_EXPIRED:   
                copy_stats(CC_id);
                insert_nr_mac_connector_struct_to_redis_database(&redis, &macControllerStruct);
                //TODO:batuhan.duyuler: if we want to configurable timer we need to setup each time it expires!
                // insert_nr_mac_connect_struct(&macControllerStruct, &db);
                break;

            case TERMINATE_MESSAGE:
                LOG_I(NR_RRC, " *** Exiting Collector thread\n");
                itti_exit_task();
            break;
            case RRC_SUBFRAME_PROCESS:
            {
                const int c_frame = (int) RRC_SUBFRAME_PROCESS(received_msg).ctxt.frame;
                const int c_subframe = (int) RRC_SUBFRAME_PROCESS(received_msg).ctxt.subframe;
                const int CC_id = (int) RRC_SUBFRAME_PROCESS(received_msg).CC_id;
                
                if (collectionOpt == SubframeBased ){
                    LOG_D(NR_RRC, "COLLECTOR task is starting on frame: %d subframe: %d\n", c_frame, c_subframe);
                    nr_collector_handle_subframe_process(c_frame, c_subframe, CC_id);
                }
            }
                break;               
            default:
            LOG_E(NR_RRC, "COLLECTOR Received unhandled message: %d:%s\n",
                ITTI_MSG_ID(received_msg), ITTI_MSG_NAME(received_msg));
            break;
            } 

        result = itti_free (ITTI_MSG_ORIGIN_ID(received_msg), received_msg);
        AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
        received_msg = NULL;
    }

    return NULL;
}

void nr_collector_trigger(int CC_id, int frame, int subframe)
{
    static uint16_t collectorTick = 0;
    collectorTick++;

    if (collectorTick != collectionInterval)
        return;

    collectorTick = 0;
    MessageDef *message_p;
    message_p = itti_alloc_new_message(TASK_COLLECTOR, 0, RRC_SUBFRAME_PROCESS);
    RRC_SUBFRAME_PROCESS(message_p).ctxt.instance = 0;
    RRC_SUBFRAME_PROCESS(message_p).ctxt.subframe = subframe;
    RRC_SUBFRAME_PROCESS(message_p).ctxt.frame = frame;
    RRC_SUBFRAME_PROCESS(message_p).CC_id = CC_id;
    itti_send_msg_to_task(TASK_COLLECTOR, moduleP, message_p);
}
