#include "db.h"
#include "db_priv.h"
#include "string+.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>

#define SQLITE_ENTRY_BLOCK_SIZE 100

// TODO better abstract interface to implement new vendors

// connection function
static db_error_t db_connect_function_sqlite(db_t *db){

	// TODO
	// https://stackoverflow.com/questions/10325683/can-i-read-and-write-to-a-sqlite-database-concurrently-from-multiple-connections
	// sqlite3_enable_shared_cache()

	db->state = db_state_not_connected;

	// check first as sync
	sqlite3 *conn;
	int errcode = sqlite3_open_v2(db->database, &conn, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if(errcode != SQLITE_OK){
		// db_error_set_message(db, "Connection to database failed", PQerrorMessage(conn));
		db->state = db_state_failed_connection;
		sqlite3_close_v2(conn);
		return db_error(db_error_connection_error, "Error connecting to db: %s", sqlite3_errstr(errcode));
    }

	// create other connections
	sqlite3 **connections = calloc(db->context.connections_count, sizeof(sqlite3*));
	db->context.connections = (void**)connections;
	db->context.available_connection = 0;
	connections[0] = conn;

	for(size_t i = 1; i < db->context.connections_count; i++){
		errcode = sqlite3_open_v2(db->database, &(connections[i]), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
		if(errcode != SQLITE_OK){
			for(int64_t j = i - 1; j >= 0; j--){
				sqlite3_close_v2(connections[i]);
			}

			db->state = db_state_failed_connection;
			return db_error(db_error_connection_error, "Error connecting to db: %s", sqlite3_errstr(errcode));
		}
	}

	db->state = db_state_connecting;
	return db_error(db_error_ok, "Connection sucessful: %s", sqlite3_errstr(errcode));
}

// stat connection
static db_state_t db_stat_function_sqlite(db_t *db){
	sqlite3 **connections = (sqlite3 **)db->context.connections;

	if(connections != NULL){

		bool all_ok = true;

		for(size_t i = 0; i < db->context.connections_count; i++){
			if(connections[i] == NULL)												// any null connection is game over
				return db_state_invalid_db;
		}

		if(all_ok){
			db->state = db_state_connected;											// every connection was ok, then connected
			return db_state_connected;
		}
		else{
			return db->state;														// if any other postgres state then return current db state
		}
	}
	else{
		return db_state_invalid_db;													// no connections = game over
	}
}

// close db connection
static void db_destroy_function_sqlite(db_t *db){
	sqlite3 **connections = (sqlite3 **)db->context.connections;

	if(connections != NULL){
		for(size_t i = 0; i < db->context.connections_count; i++){
			if(connections[i] != NULL)
				sqlite3_close(connections[i]);
		}
	}

	free(db->context.connections);
}

// map sqlite type to data types
static db_type_t db_type_map_sqlite(int type){
	switch(type){
		case SQLITE_INTEGER:
			return db_type_int;

		case SQLITE_FLOAT:
			return db_type_float;

		case SQLITE_TEXT:
			return db_type_string;

		case SQLITE_BLOB:
			return db_type_blob;

		case SQLITE_NULL:
			return db_type_null;

		default:
			return db_type_invalid;
	}
}

// error map
static db_error_code_t db_error_map_sqlite(int code){
	switch(code){
		default: 						return db_error_unknown;
		case SQLITE_ERROR:        		return db_error_fatal; /* Generic error */
		case SQLITE_PERM:         		return db_error_fatal; /* Access permission denied */
		case SQLITE_NOMEM:        		return db_error_fatal; /* A malloc() failed */
		case SQLITE_ABORT:        		return db_error_fatal; /* Callback routine requested an abort */
		
		case SQLITE_INTERRUPT:    		return db_error_fatal; /* Operation terminated by sqlite3_interrupt()*/
		case SQLITE_MISUSE:       		return db_error_fatal; /* Library used incorrectly */
		case SQLITE_NOLFS:        		return db_error_fatal; /* Uses OS features not supported on host */
		case SQLITE_AUTH:         		return db_error_fatal; /* Authorization denied */
		
		case SQLITE_READONLY:     		return db_error_fatal; /* Attempt to write a readonly database */
		case SQLITE_IOERR:        		return db_error_fatal; /* Some kind of disk I/O error occurred */
		case SQLITE_CORRUPT:      		return db_error_fatal; /* The database disk image is malformed */
		case SQLITE_NOTFOUND:     		return db_error_fatal; /* Unknown opcode in sqlite3_file_control() */
		case SQLITE_FULL:         		return db_error_fatal; /* Insertion failed because database is full */
		case SQLITE_PROTOCOL:     		return db_error_fatal; /* Database lock protocol error */

		case SQLITE_NOTICE:       		return db_error_info; /* Notifications from sqlite3_log() */
		case SQLITE_WARNING:      		return db_error_info; /* Warnings from sqlite3_log() */
		
		case SQLITE_CANTOPEN:     		return db_error_invalid_db; /* Unable to open the database file */
		case SQLITE_NOTADB:       		return db_error_invalid_db; /* File opened that is not a database file */

		case SQLITE_LOCKED:       		return db_error_processing; /* A table in the database is locked */
		case SQLITE_BUSY:         		return db_error_processing; /* The database file is locked */

		case SQLITE_RANGE:        		return db_error_invalid_range; /* 2nd parameter to sqlite3_bind out of range */

		case SQLITE_TOOBIG:       		return db_error_max; /* String or BLOB exceeds size limit */
		case SQLITE_CONSTRAINT:   		return db_error_unique_constrain_violation; /* Abort due to constraint violation */
		case SQLITE_MISMATCH:     		return db_error_invalid_type; /* Data type mismatch */

		case SQLITE_ROW:          		return db_error_ok; /* sqlite3_step() has another row ready */
		case SQLITE_OK:           		return db_error_ok; /* Successful result */
		case SQLITE_DONE:         		return db_error_ok; /* sqlite3_step() has finished executing */
	}
}

// process entries
static db_results_t *db_process_query_sqlite(const db_t *db, sqlite3_stmt *stmt){
	db_results_t *results = db_results_new(db->vendor, 0, 0, db_error_ok, NULL);
	results->ctx = stmt;

	// step
	results->entries_count = 0;
	results->fields_count = sqlite3_column_count(stmt);

	int64_t entries_allocated = SQLITE_ENTRY_BLOCK_SIZE;
	if(results->fields_count > 0){
		// fields names
		results->fields = malloc(sizeof(char*) * results->fields_count);
		for(int64_t j = 0; j < results->fields_count; j++)
			results->fields[j] = (char*)sqlite3_column_name(stmt, j);

		// entries
		results->entries = malloc(sizeof(db_field_t*) * entries_allocated);
	} 
	
	bool more = true;
	while(more){
		int code = sqlite3_step(stmt);
		switch(code){
			case SQLITE_BUSY:														// on busy, wait
				continue;

			case SQLITE_DONE:														// same as SQLITE_ROW, but dont loop again
				more = false;
			case SQLITE_ROW:														// new row
				// result values
				if(results->fields_count > 0)
					results->entries[results->entries_count] = malloc(sizeof(db_field_t) * results->fields_count);

				// for every column/field
				for(int64_t j = 0; j < results->fields_count; j++){
					db_field_t entry = {0};

					// get type
					entry.type = db_type_map_sqlite(sqlite3_column_type(stmt, j));

					switch(entry.type){
						default:														// types with no value, just leave as
						case db_type_invalid:
						case db_type_null:
						break;

						case db_type_int:
							entry.size = sizeof(int64_t);
							entry.value.as_int = sqlite3_column_int64(stmt, j);
						break;

						case db_type_float:
							entry.size = sizeof(double);
							entry.value.as_float = sqlite3_column_double(stmt, j);
						break;
							
						case db_type_string:
							entry.size = sizeof(char) * sqlite3_column_bytes(stmt, j);
							entry.value.as_string = (char*)sqlite3_column_text(stmt, j);
						break;

						case db_type_blob:
							entry.size = sizeof(uint8_t) * sqlite3_column_bytes(stmt, j);
							entry.value.as_blob = (uint8_t*)sqlite3_column_blob(stmt, j);
						break;
					}
					
					// save entry
					results->entries[results->entries_count][j] = entry;
				}

				// expand
				results->entries_count++;
				if(results->entries_count >= entries_allocated){
					entries_allocated += SQLITE_ENTRY_BLOCK_SIZE;
					results->entries = realloc(results->entries, sizeof(db_field_t*) * entries_allocated);
				}
			break;

			default:
				db_results_set_error(results, db_error_map_sqlite(code), "Error executing query: %s", sqlite3_errstr(code));
			return results;
			break;
		}
	}

	db_results_set_error(results, db_error_ok, "%s", sqlite3_errstr(SQLITE_OK));
	return results;
}

// db exec
static db_results_t *db_exec_function_sqlite(const db_t *db, void *connection, char *query, size_t params_count, va_list params){
	sqlite3 *conn = (sqlite3*)connection;
	sqlite3_stmt *stmt;
	sqlite3_prepare_v3(conn, query, strlen(query), 0, &stmt, NULL);

	// process params 
	for(size_t i = 0; i < params_count; i++){									// for each param
		db_field_t param = va_arg(params, db_field_t);

		switch(param.type){
			case db_type_int:												// integer
				sqlite3_bind_int64(stmt, i + 1, param.value.as_int);
			break;

			case db_type_bool:   											// bool
				sqlite3_bind_int64(stmt, i + 1, param.value.as_bool);
			break;

			case db_type_float:  											// float
				sqlite3_bind_double(stmt, i + 1, param.value.as_float);
			break;

			case db_type_string: 											// string
				sqlite3_bind_text(stmt, i + 1, param.value.as_string, param.size, SQLITE_TRANSIENT);
			break;

			case db_type_blob:												// blob
				sqlite3_bind_text(stmt, i + 1, param.value.as_blob, param.size, SQLITE_TRANSIENT);
			break;

			default:
			case db_type_invalid:
			case db_type_null:   											// null
				sqlite3_bind_null(stmt, i + 1);
			break;
		}
	}

	// exec query 
	db_results_t *results = db_process_query_sqlite(db, stmt);
	return results;
}

static void db_result_destroy_context_sqlite(void *ctx){
	sqlite3_finalize((sqlite3_stmt*)ctx);
}
