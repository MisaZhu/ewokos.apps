/*
 *  rpc_stub.c - stub for the Unix GUI RPC layer (not used on EwokOS)
 */

#include <stddef.h>
#include <stdarg.h>

#include "rpc.h"

rpc_connection_t *rpc_init_client(const char *ident)
{
	(void)ident;
	return NULL;
}

int rpc_method_invoke(rpc_connection_t *connection, int method, ...)
{
	(void)connection;
	(void)method;
	return RPC_ERROR_CONNECTION_NULL;
}

int rpc_method_wait_for_reply(rpc_connection_t *connection, ...)
{
	(void)connection;
	return RPC_ERROR_CONNECTION_NULL;
}
