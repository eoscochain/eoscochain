#pragma once

#ifdef __cplusplus
extern "C" {
#endif


int timestamp_txid_seed( const char* sig, size_t siglen );

int producer_random_seed( const char* sig, size_t siglen );

#ifdef __cplusplus
}
#endif
