#include <stdlib.h>
#include <stdio.h>
#include "db.h"
#include "db_priv.h"
#include "string+.h"
#include "db_postgres.h"
#include "db_sqlite.h"

// ------------------------------------------------------------ Invalid database default

// default connection function
static db_error_t db_default_function_invalid(db_t *db){
	db->state = db_state_invalid_db;
	return db_error(db_error_invalid_db, "Non supported db vendor");
}

// ------------------------------------------------------------- Private calls  ----------------------------------------------------

// create new error fmt
db_error_t db_error_new_valist(db_error_code_t code, const char *fmt, va_list args){
	db_error_t err = {0};
	err.code = code;
	
	if(fmt != NULL)
		vsnprintf(err.msg, DB_MSG_LEN - 1, fmt, args);

	return err;
}

// create new error fmt
db_error_t db_error(db_error_code_t code, const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	db_error_t err = db_error_new_valist(code, fmt, args);
	va_end(args);
	return err;
}

// set result error
void db_results_set_error(db_results_t *results, db_error_code_t code, const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	results->error = db_error_new_valist(code, fmt, args);
	va_end(args);
}

// create new result fmt
db_results_t *db_results_new(db_vendor_t vendor, int64_t entries, int64_t fields, db_error_code_t code, char *fmt, ...){
	va_list args;
	va_start(args, fmt);

	db_results_t *result = calloc(1, sizeof(db_results_t));

	result->entries_count = entries;
	result->fields_count = fields;
	result->vendor = vendor;
	result->error = db_error_new_valist(code, fmt, args);

	va_end(args);
	return result;
}

// create new result object for null db
db_results_t *db_results_error_nulldb(){
	return db_results_new(db_vendor_invalid, 0, 0, db_error_invalid_db, "Database passed was null");
}

// ------------------------------------------------------------ Database maps ------------------------------------------------------

// vendor name map
// static const char *db_vendor_name_map(db_vendor_t vendor){
// 	switch(vendor){
// 		default: return "Invalid DB vendor";
// 		case db_vendor_postgres: 
// 		case db_vendor_postgres15: 
// 			return "Postgres";
		
// 		case db_vendor_sqlite:
// 			return "SQLite";
// 	}
// }

// map error codes
db_error_code_t db_error_map(db_vendor_t vendor, int code){
	switch(vendor){
		default: 						
			return db_error_invalid_db;

		case db_vendor_postgres: 		
		case db_vendor_postgres15: 		
			return db_error_map_postgres(code);

		case db_vendor_sqlite: 		
			return db_error_map_sqlite(code);
	}
}

// connection functions
db_error_t db_connect_function_map(db_t *db){
	if(db == NULL) return db_error(db_error_invalid_db, "Db object was null");
	
	switch(db->state){
		// only try connecting/reconnecting
		case db_state_not_connected:
		case db_state_failed_connection:
			break;

		case db_state_connected:
			return db_error(db_error_ok, "Ok");

		case db_state_connecting:
			return db_error(db_error_processing, "Busy...");
		
		default:
		case db_state_invalid_db:
			return db_error(db_error_invalid_db, "Db object state is invalid");
	}

	switch(db->vendor){
		default: 
			return db_default_function_invalid(db);
			
		case db_vendor_postgres:
		case db_vendor_postgres15:
			return db_connect_function_postgres(db);

		case db_vendor_sqlite:
			return db_connect_function_sqlite(db);
	}

	return db_error(db_error_unknown, "Unknown error");
}

// poll current db status. map
db_state_t db_stat_function_map(db_t *db){
	if(db == NULL) return db_state_invalid_db;
	
	switch(db->vendor){
		default: 
			return db_state_invalid_db;
			
		case db_vendor_postgres:
		case db_vendor_postgres15:
			return db_stat_function_postgres(db);

		case db_vendor_sqlite:
			return db_stat_function_sqlite(db);
	}
}

// close db map
void db_destroy_function_map(db_t *db){
	if(db == NULL) return;
	
	// context free
	switch(db->vendor){
		default: return;
			
		case db_vendor_postgres:
		case db_vendor_postgres15:
			db_destroy_function_postgres(db);

		case db_vendor_sqlite:
			db_destroy_function_sqlite(db);
	}

	free(db);
}

// exec query map
static db_results_t *db_exec_function_map(const db_t *db, void *connection, char *query, size_t params_count, va_list params){
	if(db == NULL) return db_results_error_nulldb();

	switch(db->vendor){
		default: 
			return db_results_new(db->vendor ,0, 0, db_error_invalid_db, "Vendor not yet implemented");
			
		case db_vendor_postgres:
		case db_vendor_postgres15:
			return db_exec_function_postgres(db, connection, query, params_count, params);

		case db_vendor_sqlite:
			return db_exec_function_sqlite(db, connection, query, params_count, params);
	}
}

// port map 
static char *db_default_port_map(db_vendor_t vendor){
	switch(vendor){
		case db_vendor_postgres:
		case db_vendor_postgres15:
			return "5432";

		default:
			return "3306";
	}
}

// result context destroy map
void db_result_destroy_context_map(db_vendor_t vendor, void *ctx){
	switch(vendor){
		case db_vendor_postgres15:
		case db_vendor_postgres:
			db_result_destroy_context_postgres(ctx);
			return;

		case db_vendor_sqlite:
			db_result_destroy_context_sqlite(ctx);
			return;

		default:
			return;
	}
}

// ------------------------------------------------------------- Public calls ------------------------------------------------------

// create a new db object
db_t *db_create(db_vendor_t type, size_t num_connections, char *host, char *port, char *database, char *user, char *password, char *role, db_error_code_t *code){
	if(
		(database == NULL) ||
		(num_connections < 1)
	){
		if(code != NULL) *code = db_error_invalid_db;
		return NULL;
	}

	// create db
	db_t *db = (db_t*)calloc(1, sizeof(db_t));

	// add mutex to connection bool
	db->context.connections_count = num_connections;
	if(pthread_mutex_init(&(db->context.connections_lock), NULL) != 0){
		if(code != NULL) *code = db_error_unknown;
		free(db);
		return NULL;
	}

	// set
	db->vendor		= type;
	db->host     	= host;
	db->port     	= port != NULL ? port : db_default_port_map(type);
	db->database 	= database;
	db->user     	= user;
	db->password 	= password != NULL ? password : "";
	db->role     	= role != NULL ? role : "";
	db->state 		= db_state_not_connected;
	
	if(code != NULL) *code = db_error_ok;
	return db;
}

// connect to database
db_error_t db_connect(db_t *db){
	return db_connect_function_map(db);
}

// poll current db status. Use this function before accessing db->state
db_state_t db_stat(db_t *db){
	return db_stat_function_map(db);
}

// new integer param for query
db_field_t db_param_integer(int64_t value){
	return (db_field_t){
		.type = db_type_int,
		.size = sizeof(int64_t),
		.value.as_int = value
	}; 
}

// new bool param for query
db_field_t db_param_bool(bool value){
	return (db_field_t){
		.type = db_type_bool,
		.size = sizeof(bool),
		.value.as_bool = value
	}; 
}

// new float param for query
db_field_t db_param_float(double value){
	return (db_field_t){
		.type = db_type_float,
		.size = sizeof(double),
		.value.as_float = value
	}; 
}

// new string param for query
db_field_t db_param_string(char *value, size_t len){
	return (db_field_t){
		.type = db_type_string,
		.size = sizeof(char) * len,
		.value.as_string = value
	}; 
}

// new binary param for query
db_field_t db_param_blob(void *value, size_t size){
	return (db_field_t){
		.type = db_type_string,
		.size = size,
		.value.as_string = value
	};
}

// new null param for query
db_field_t db_param_null(){
	return (db_field_t){
		.type = db_type_null
	}; 
}

// try and get a connection
static void *db_request_conn(db_t *db){
	if(db->state != db_state_connected) return NULL;
	if(db->context.available_connection >= db->context.connections_count) return NULL;

	pthread_mutex_lock(&(db->context.connections_lock));
	
	void *available_connection = db->context.connections[db->context.available_connection];
	db->context.available_connection++;

	pthread_mutex_unlock(&(db->context.connections_lock));

	return available_connection;
}

// return used connection
static void db_return_conn(db_t *db, void *conn){
	if(db->context.available_connection == 0) return;
	
	pthread_mutex_lock(&(db->context.connections_lock));

	db->context.available_connection--;
	db->context.connections[db->context.available_connection] = conn; 

	pthread_mutex_unlock(&(db->context.connections_lock));
}

// exec query
db_results_t *db_exec(db_t *db, char *query, size_t params_count, ...){
	va_list params;
	va_start(params, params_count);
	
	void *conn;
	int retries = DB_CONN_POOL_RETRY;
	while(retries){
		conn = db_request_conn(db);

		if(conn == NULL)
			retries--;
		else
			break;
	}

	if(retries == 0 && conn == NULL){
		va_end(params);
		return db_results_new(db->vendor, 0, 0, db_error_fatal, "Could not get connnection from connection pool. Connection available: [%lu]. Connection count: [%lu]", db->context.available_connection, db->context.connections_count);
	}

	db_results_t *res = db_exec_function_map(db, conn, query, params_count, params);

	db_return_conn(db, conn);

	va_end(params);
	return res;
}

// exec multiple queries
db_results_t *db_exec_script(db_t *db, char *queries){
	string *queriesStr = string_wrap(queries, false);

	string_ite ite = string_split(queriesStr, ";");
	db_results_t *res;
	int i = 0;

	foreach(string, query, ite){
		res = db_exec(db, query.raw, 0);
		i++;
	}
	
	string_destroy(queriesStr);

	if(i == 0){
		
	}

}

// destroy results
void db_results_destroy(db_results_t *results){
	if(results == NULL) return;

	if(results->entries_count > 0){
		if(results->fields != NULL){
			for(int64_t i = 0; i < results->entries_count; i++)						// for each entry
				free(results->entries[i]);											// free whole entry row
		}

		free(results->entries);
		free(results->fields);
	}

	db_result_destroy_context_map(results->vendor, results->ctx);
	free(results);
}

// close db connection
void db_destroy(db_t *db){
	db_destroy_function_map(db);
}

// get single field
db_field_t db_read_field(db_results_t *results, uint32_t entry, uint32_t field){
	if(results->entries == NULL) return (db_field_t){.type = db_type_invalid};
	if(entry >= results->entries_count) return (db_field_t){.type = db_type_invalid};
	if(field >= results->fields_count) return (db_field_t){.type = db_type_invalid};
	if(results->entries[entry][field].type == db_type_invalid) return (db_field_t){.type = db_type_invalid};
	return results->entries[entry][field];
}

// is is invalid
bool db_field_isInvalid(db_results_t *results, uint32_t entry, uint32_t field){
	return db_read_field(results, entry, field).type == db_type_invalid;
}

// print
void db_print_results(db_results_t *results){
	if(results->entries == NULL) return;

	for(int64_t i = 0; i < results->entries_count; i++){
		// columns names
		if(i == 0){
			for(int64_t j = 0; j < results->fields_count; j++){
				printf("| %s ", results->fields[j]);

				if(j == (results->fields_count - 1)){
					printf("|\n");
				}
			}	
		}

		// entry
		for(int64_t j = 0; j < results->fields_count; j++){
			printf("| ");

			switch(results->entries[i][j].type){
				default:
				case db_type_invalid:
				case db_type_null:
					printf("null");
				break;

				case db_type_int:
					printf("%ld", db_read_field(results, i, j).value.as_int);
				break;

				case db_type_bool:
					printf("%s", db_read_field(results, i, j).value.as_bool ? "true" : "false");
				break;

				case db_type_float:
					printf("%f", db_read_field(results, i, j).value.as_float);
				break;

				case db_type_string:
					printf("\"%s\"", db_read_field(results, i, j).value.as_string);
				break;

				case db_type_blob:
					printf("[BLOB] size: %lu", (long unsigned int)db_read_field(results, i, j).size);
				break;
			}

			printf(" ");

			if(j == (results->fields_count - 1)){
				printf("|\n");
			}
		}	
	}
}

// // print json
// char *db_json_entries(db_results_t *results, bool squash_if_single){
// 	if(results->entries == NULL) return NULL;

// 	bool trail = !(squash_if_single && results->entries_count == 1);

// 	string *json = string_new();

// 	if(trail)
// 		string_cat_raw(json, "[");
// 	else
// 		string_cat_raw(json, "{");

// 	for(size_t i = 0; i < results->entries_count; i++){
// 		if(trail)
// 			string_cat_raw(json, "{");
	
// 		for(size_t j = 0; j < results->fields_count; j++){

// 			if(j != 0)
// 				string_cat_raw(json, ",");

// 			// name
// 			string_cat_fmt(json, "\"%s\":", 50, results->fields[j]);
			
// 			// value
// 			switch(results->entries[i][j].type){
// 				default:
// 				case db_type_invalid:
// 				case db_type_null:
// 					string_cat_fmt(json, "null", 5);
// 					break;

// 				case db_type_int:
// 					string_cat_fmt(json, "%d", 15, *db_results_read_integer(results, i, j));
// 					break;

// 				case db_type_bool:
// 					string_cat_fmt(json, "%d", 15, *db_results_read_bool(results, i, j));
// 					break;
					

// 				case db_type_float:
// 					string_cat_fmt(json, "%f", 20, *db_results_read_float(results, i, j));
// 					break;

// 				case db_type_string:
// 					string_cat_fmt(json, "\"%s\"", 255, db_results_read_string(results, i, j));
// 					break;

// 				// case db_type_blob:

// 				case db_type_int_array:
// 				{
// 					uint32_t count;
// 					int **array = db_results_read_integer_array(results, i, j, &count);

// 					string_cat_raw(json, "[");
// 					for(uint32_t k = 0; k < count; k++){
// 						if(k != 0)
// 							string_cat_raw(json, ",");

// 						string_cat_fmt(json, "%d", 15, *(array[k]));
// 					}

// 					string_cat_raw(json, "]");
// 				}
// 				break;

// 				case db_type_string_array:
// 				{
// 					uint32_t count;
// 					char **array = db_results_read_string_array(results, i, j, &count);

// 					string_cat_raw(json, "[");
// 					for(uint32_t k = 0; k < count; k++){
// 						if(k != 0)
// 							string_cat_raw(json, ",");

// 						string_cat_fmt(json, "\"%s\"", 255, array[k]);
// 					}

// 					string_cat_raw(json, "]");
// 				}
// 				break;

// 				// case db_type_bool_array:
// 				// case db_type_float_array:

// 				// case db_type_blob_array:
// 			}
// 		}	

// 		if(trail)
// 			string_cat_raw(json, "}");

// 		if(i != (results->entries_count - 1))
// 			string_cat_raw(json, ",");
// 	}

// 	if(trail)
// 		string_cat_raw(json, "]");
// 	else
// 		string_cat_raw(json, "}");

// 	char *ret = strdup(json->raw);
// 	string_destroy(json);

// 	return ret;
// }
