#ifndef COLLECTOR_DB_MANAGER_H_
#define COLLECTOR_DB_MANAGER_H_

#include <stdio.h>
#include <sqlite3.h>
#include <hiredis/hiredis.h>

struct NR_mac_collector_struct;
struct NR_mac_collector_UE_info;
struct NR_mac_collector_CC_info;

// MYSQL Functions
void create_connector_database(sqlite3** db);
//calculate delta value before writing to database.
int insert_ue_object(struct NR_mac_collector_UE_info* ue, sqlite3* db);
int insert_nr_mac_connect_struct(struct NR_mac_collector_struct* collector_struct, sqlite3** db);

// REDIS Functions
int create_connector_redis_database(redisContext** redis, const char* host, uint32_t port);
int redis_database_update_ue(redisContext* redis, struct NR_mac_collector_UE_info* ue);
int redis_database_update_cell(redisContext* redis, struct NR_mac_collector_CC_info* cell);
int insert_nr_mac_connector_struct_to_redis_database(redisContext** redis, struct NR_mac_collector_struct* collector_struct);

#endif