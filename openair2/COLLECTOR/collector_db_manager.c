#include <stdio.h>
#include <time.h>
#include <sqlite3.h>
#include "collector.h"

#define REDIS_DB_FLUSH_LIMIT 180

extern NR_mac_collector_struct_t macControllerStruct;
extern NR_mac_collector_struct_t macControllerStruct_prev;
extern bool intervalChangedFlag;


int create_connector_redis_database(redisContext** redis, const char* host, uint32_t port)
{
    *redis = redisConnect(host, port);
    if ((*redis) == NULL || (*redis)->err) {
        printf("Failed to connect to Redis: %s\n", (*redis)->errstr);
        return -1;
    }

    redisReply *reply = redisCommand((*redis), "FLUSHDB");
    if(reply == NULL || reply->type == REDIS_REPLY_ERROR) {
        printf("Failed to flush Redis database\n");
        freeReplyObject(reply);
        redisFree((*redis));
        return EXIT_FAILURE;
    }
    freeReplyObject(reply);
    
    return 0;
}
int redis_database_update_cell(redisContext* redis, struct NR_mac_collector_CC_info* cell){
    char cellKey[30];
    time_t current_time = time(NULL);
    char timestamp[20];
    strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", localtime(&current_time));
    sprintf(cellKey, "%d:%s", cell->carrierId, timestamp);

    redisReply* reply = redisCommand(redis, "EXISTS %s", cellKey);
    if (reply == NULL || reply->type == REDIS_REPLY_ERROR) {
        LOG_E(NR_RRC, "COLLECTOR: Redis Error Exist Check \n");
        freeReplyObject(reply);
        return -1;
    }

    int exists = reply->integer;
    freeReplyObject(reply);
    if (exists) {
        // Update existing cell!
        reply = redisCommand(redis, "HSET cellKey:%s timestamp %s cellId %d dl_prb_usg %f ul_prb_usg %f"
                                    " slot_interval %d time_interval(ms) %d downlink_bw %u uplink_bw %u"
                                    " dl_carrier_freq(Hz) %lu SCS(kHz) %u UL_throughput(Kbps) %f"
                                    " DL_throughput(Kbps) %f num_connected_device %d"
                                    " dl_freq_band %d tracking_area_code %d",
            cellKey, timestamp, cell->carrierId, cell->dl_prb_usg, cell->ul_prb_usg,
            cell->slotsPassed, cell->timePassed, cell->dlBw, cell->ulBw, cell->dlCarrierFreq,
            cell->subcarrierSpacing, cell->ul_throughput, cell->dl_throughput, cell->num_connected_device,
            cell->dlFrequencyBand, cell->tracking_area_code);
    } else {
        // Insert new cell!
        reply = redisCommand(redis, "HMSET cellKey:%s timestamp %s cellId %d dl_prb_usg %f ul_prb_usg %f"
                                    " slot_interval %d time_interval(ms) %d downlink_bw %u uplink_bw %u"
                                    " dl_carrier_freq(Hz) %lu SCS(kHz) %u UL_throughput(Kbps) %f"
                                    " DL_throughput(Kbps) %f num_connected_device %d"
                                    " dl_freq_band %d tracking_area_code %d",
            cellKey, timestamp, cell->carrierId, cell->dl_prb_usg, cell->ul_prb_usg,
            cell->slotsPassed, cell->timePassed, cell->dlBw, cell->ulBw, cell->dlCarrierFreq,
            cell->subcarrierSpacing, cell->ul_throughput, cell->dl_throughput, cell->num_connected_device,
            cell->dlFrequencyBand, cell->tracking_area_code);

    }
        
    if (reply == NULL || reply->type == REDIS_REPLY_ERROR) {
            LOG_E(NR_RRC, "COLLECTOR: Redis Error with exist flag: %d \n", exists);
            freeReplyObject(reply);
            return -1;
    }

    freeReplyObject(reply);
    return 0;
}

int redis_database_update_ue(redisContext* redis, struct NR_mac_collector_UE_info* ue){
    uint8_t CC_id = 0; //for now
    char ueKey[30];
    time_t current_time = time(NULL);
    char timestamp[20];
    strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", localtime(&current_time));
    sprintf(ueKey, "%d:%d:%s", ue->rnti, CC_id, timestamp);

    // Check if the ue exists in Redis
    redisReply* reply = redisCommand(redis, "EXISTS %s", ueKey);
    if (reply == NULL || reply->type == REDIS_REPLY_ERROR) {
        LOG_E(NR_RRC, "COLLECTOR: Redis Error Exist Check \n");
        freeReplyObject(reply);
        return -1;
    }

    int exists = reply->integer;
    freeReplyObject(reply);
    if (exists) {
        // Update existing ue!
        reply = redisCommand(redis, "HSET ueKey:%s timestamp %s CC_id %d mac_uid %d ran_ue_ngap_id %d amf_ngap_id %d rnti %d dl_total_rbs %d ul_total_rbs %d "
                                    "dl_total_bytes %d ul_total_bytes %d lcid4_dl_bytes %d lcid4_ul_bytes %d "
                                    "dl_throughput %f ul_throughput %f rrc_status %d tac %d",
         ueKey, timestamp, ue->CC_id, ue->mac_uid, ue->ran_ue_ngap_id, ue->amf_ue_ngap_id, ue->rnti, ue->dl_total_rbs, ue->ul_total_rbs,
         ue->dl_total_bytes, ue->ul_total_bytes, ue->dl_lc_bytes[4], ue->ul_lc_bytes[4],
         ue->dl_thr_ue, ue->ul_thr_ue, ue->rrc_status, ue->current_tac);

    } else {
        // Insert new ue!
        reply = redisCommand(redis, "HMSET ueKey:%s timestamp %s CC_id %d mac_uid %d ran_ue_ngap_id %d amf_ngap_id %d rnti %d dl_total_rbs %d ul_total_rbs %d "
                                    "dl_total_bytes %d ul_total_bytes %d lcid4_dl_bytes %d lcid4_ul_bytes %d "
                                    "dl_throughput %f ul_throughput %f rrc_status %d tac %d",
         ueKey, timestamp, ue->CC_id, ue->mac_uid, ue->ran_ue_ngap_id, ue->amf_ue_ngap_id, ue->rnti, ue->dl_total_rbs, ue->ul_total_rbs,
         ue->dl_total_bytes, ue->ul_total_bytes, ue->dl_lc_bytes[4], ue->ul_lc_bytes[4],
         ue->dl_thr_ue, ue->ul_thr_ue, ue->rrc_status, ue->current_tac);
    }
        
    if (reply == NULL || reply->type == REDIS_REPLY_ERROR) {
            LOG_E(NR_RRC, "COLLECTOR: Redis Error with exist flag: %d \n", exists);
            freeReplyObject(reply);
            return -1;
    }

    freeReplyObject(reply);
    return 0;
}

int check_size_and_flush_db(redisContext* redis)
{
    redisReply *reply = redisCommand(redis, "DBSIZE");
    if (reply == NULL)
    {
        LOG_E(NR_RRC, "REDIS DB: Can not run DBSIZE command\n");
        return -1;
    }

    if (reply->type != REDIS_REPLY_INTEGER)
    {
        LOG_E(NR_RRC, "REDIS DB: DBSIZE Reply is not an integer\n");
        freeReplyObject(reply);
        return -1;        
    }

    int numDBElements = reply->integer;
    if (numDBElements <= REDIS_DB_FLUSH_LIMIT)
        return 0;

    redisReply *flushReply = redisCommand(redis, "FLUSHDB");
    if (flushReply == NULL)
    {
        LOG_E(NR_RRC, "REDIS DB: FLUSH DB is failed!\n");
        return -1;          
    }

    freeReplyObject(flushReply);
    LOG_I(NR_RRC, "REDIS DB: DB is FLUSHED because of LIMIT!\n");
    return 0;
}

void copy_mac_collector_previous_stats()
{
    uint8_t ueId = 0;
    Collector_UE_iterator(macControllerStruct.ueList, UE) {
        if (UE != NULL) {
            NR_mac_collector_UE_info_t * UE_prev = (NR_mac_collector_UE_info_t *)calloc(1,sizeof(NR_mac_collector_UE_info_t));
            UE_prev->rnti = UE->rnti;
            UE_prev->prev_dl_total_rbs = UE->prev_dl_total_rbs;
            UE_prev->prev_ul_total_rbs = UE->prev_ul_total_rbs;
            UE_prev->prev_dl_total_bytes = UE->prev_dl_total_bytes;
            UE_prev->prev_ul_total_bytes = UE->prev_ul_total_bytes;
            UE_prev->prev_dl_lc_bytes[4] = UE->prev_dl_lc_bytes[4];
            UE_prev->prev_ul_lc_bytes[4] = UE->prev_ul_lc_bytes[4];
            macControllerStruct_prev.ueList[ueId] = UE_prev;
            ueId++;
        }
    }    
}

int insert_nr_mac_connector_struct_to_redis_database(redisContext** redis, struct NR_mac_collector_struct* collector_struct)
{
    copy_mac_collector_previous_stats();

    if (intervalChangedFlag == true) {
        intervalChangedFlag = false;
        return 0;
    }

    int size_check = check_size_and_flush_db(*redis);
    if (size_check != 0)
    {
        LOG_E(NR_RRC, "REDIS DB: Size Check Failed!\n");
        return -1;
    }

    
    Collector_UE_iterator(collector_struct->ueList, UE) {
        if (UE != NULL) {
            int rc = redis_database_update_ue(*redis, UE);
            if (rc != 0){
                LOG_E(NR_RRC, "Can not insert ue data to redis database\n");
                return rc;
            }            
        }
    }

    Collector_CELL_iterator(collector_struct->ccList, Cell) {
        if (Cell != NULL) {
            int rc = redis_database_update_cell(*redis, Cell);
            if (rc != 0){
                LOG_E(NR_RRC, "Can not insert cell data to redis database\n");
                return rc;
            }   
        }
    }

    return 0;
}




// ---- SQLITE FUNCTIONS ---- //
void create_connector_database(sqlite3** db)
{
    int rc = sqlite3_open("../../../../batuhan/collector_db.db", &(*db));
    if (rc != SQLITE_OK) {
        LOG_E(NR_RRC,"Cannot open database: %s\n", sqlite3_errmsg(*db));
        sqlite3_close(*db);
        exit(1);
    }

    char* sql = "DROP TABLE IF EXISTS ue_database; CREATE TABLE IF NOT EXISTS ue_database (id INTEGER PRIMARY KEY AUTOINCREMENT,timestamp DATETIME, rnti INT,total_rbs INT, curr_rbs INT);";
    rc = sqlite3_exec(*db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        LOG_E(NR_RRC,"Cannot create table: %s\n", sqlite3_errmsg(*db));
        sqlite3_close(*db);
        exit(1);
    }
    LOG_I(NR_RRC, "DATABASE is created!\n");
}

int insert_ue_object(struct NR_mac_collector_UE_info* ue, sqlite3* db)
{
    time_t current_time = time(NULL);
    char timestamp[20];
    strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", localtime(&current_time));
    
    const int total_rbs =  ue->dl_total_rbs;
    const int curr_rbs  =  ue->dl_current_rbs;
    const int rnti = ue->rnti;
    if (db == NULL){
        LOG_W(NR_RRC, "DB is NULL\n");
    }
    char *sql = sqlite3_mprintf("INSERT INTO ue_database (timestamp, rnti, curr_rbs, total_rbs) VALUES ('%q', %d, %d, %d);", timestamp, rnti, curr_rbs, total_rbs);
    int rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_free(sql);

    return rc;
}

int insert_nr_mac_connect_struct(struct NR_mac_collector_struct* collector_struct, sqlite3** db)
{
    int ueNumber = 0;

    while (collector_struct->ueList[ueNumber] != NULL)
    {
        int rc = insert_ue_object(collector_struct->ueList[ueNumber], *db);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Cannot insert data for ue :%d is : %s\n",ueNumber, sqlite3_errmsg(*db));
            return rc;
        }
        ueNumber++;
    }
    return SQLITE_OK;
}
