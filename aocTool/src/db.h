#ifndef _DB_HEADER_
#define _DB_HEADER_

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <pthread.h>

#define DB_MSG_LEN 300
#define DB_CONN_POOL_RETRY 5

// ------------------------------------------------------------ Types --------------------------------------------------------------

// db type
typedef enum{
	db_vendor_postgres,
	db_vendor_postgres15,
	db_vendor_sqlite,
	// db_vendor_mysql,
	// db_vendor_firebird,
	// db_vendor_cassandra,
	db_vendor_invalid
}db_vendor_t;

// the type for the query parameter
typedef enum{
	db_type_invalid = -1,
	db_type_null = 0,
	db_type_int,
	db_type_bool,
	db_type_float,
	db_type_string,
	db_type_blob,
	db_type_max
}db_type_t;

// db param used in the query
typedef struct{
	db_type_t type;
	size_t size;

	union{
		int64_t   as_int;
		bool      as_bool;
		double    as_float;
		char*     as_string;
		void*     as_blob;
	}value;
}db_field_t;

// error codes
typedef enum{
	db_error_ok = 0,
	db_error_unique_constrain_violation,
	db_error_not_null_constrain_violation,
	db_error_foreign_key_constrain_violation,
	db_error_check_constrain_violation,
	db_error_invalid_type,
	db_error_invalid_range,
	db_error_processing,
	db_error_info,
	db_error_fatal,
	db_error_connection_error,
	db_error_unknown,
	db_error_invalid_db,
	db_error_max
}db_error_code_t;

// error struct
typedef struct{
	db_error_code_t code;
	char msg[DB_MSG_LEN];
}db_error_t;

// TODO better way to handle error handling in results
// TODO try to remove vendor froim results. Used in the 'results_destroy' to call implementantation specific context destroy
// returned after database execution calls
typedef struct{
	db_vendor_t vendor;
	int64_t fields_count;
	char **fields;
	int64_t entries_count;
	db_field_t **entries;

	void *ctx;								/**< Implementation specific object. Eg. Potgres: PGResult*, Sqlite: sqlite3_stmt * */
	
	db_error_t error;
}db_results_t;

// current state of the db object
typedef enum{
	db_state_invalid_db = -1,
	db_state_not_connected = 0,
	db_state_connecting,
	db_state_connected,
	db_state_failed_connection
}db_state_t;

// db struct
typedef struct{
	db_vendor_t vendor;						/**< db type */
	char *host;    							/**< db host */
	char *port;    							/**< db host port */
	char *database;							/**< db database */
	char *user;    							/**< db user */
	char *password;							/**< db user password */
	char *role;    							/**< db user role */

	db_state_t state;						/**< enum of the current database state. Call db_stat() before reading this */

	struct{									/**< struct containing the connection pool to the database */
		pthread_mutex_t connections_lock;
		size_t connections_count;
		void **connections;
		size_t available_connection;
	}context;
}db_t;

// ------------------------------------------------------------ Functions ----------------------------------------------------------

/**
 * @brief create db object
 * @param type: db_vendor_t database type
 * @param num_connections: number of concurrent connections
 * @param host: db host
 * @param port: db host port
 * @param database: db name
 * @param user: db user
 * @param password: db user password
 * @param role: db user role
 * @param code: pass a &db_error_code_t to read the out error code
 * @return database object, call db_connect() before using!
*/
db_t *db_create(db_vendor_t type, size_t num_connections, char *host, char *port, char *database, char *user, char *password, char *role, db_error_code_t *code);

/**
 * @brief connect to database
*/
db_error_t db_connect(db_t *db);

/**
 * @brief poll current db status. Use this function before accessing db->state
*/
db_state_t db_stat(db_t *db);

/**
 * @brief close db connections and frees memory
*/
void db_destroy(db_t *db);

// new integer param for query
db_field_t db_param_integer(int64_t value);

// new bool param for query
db_field_t db_param_bool(bool value);

// new float param for query
db_field_t db_param_float(double value);

// new string param for query
db_field_t db_param_string(char *value, size_t len);

// new blob param for query
db_field_t db_param_blob(void *value, size_t size);

// new null param for query
db_field_t db_param_null();

// exec a query. return is always NOT NULL, no need to check
db_results_t *db_exec(db_t *db, char *query, size_t params_count, ...);

// read field value from the results of a query. NULL if null | non existent | invalid. Use beforehand the calls db_results_isvalid() | db_results_isnull() | db_results_isvalid_and_notnull() to check if the value is what you expect
db_field_t db_read_field(db_results_t *results, uint32_t entry, uint32_t field);

// check if a value from the results of a query is invalid. Cafeful! invalid values are not null, only a valid value can be null. Valid values can be null or the value itself
bool db_field_isInvalid(db_results_t *results, uint32_t entry, uint32_t field);

// free results returned from a query
void db_results_destroy(db_results_t *results);

// print results from a query
void db_print_results(db_results_t *results);

#endif
