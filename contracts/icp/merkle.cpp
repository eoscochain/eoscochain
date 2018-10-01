#include "merkle.hpp"

namespace eosio {

/**
 * in order to keep proofs concise, before hashing we set the first bit
 * of the previous hashes to 0 or 1 to indicate the side it is on
 *
 * this relieves our proofs from having to indicate left vs right contactenation
 * as the node values will imply it
 */

digest_type make_canonical_left(const digest_type& val) {
    digest_type canonical_l = val;
    *reinterpret_cast<uint64_t*>(canonical_l.hash) &= 0xFFFFFFFFFFFFFF7FULL;
    return canonical_l;
}

digest_type make_canonical_right(const digest_type& val) {
    digest_type canonical_r = val;
    *reinterpret_cast<uint64_t*>(canonical_r.hash) |= 0x0000000000000080ULL;
    return canonical_r;
}

bool is_canonical_left(const digest_type& val) {
    return (*reinterpret_cast<const uint64_t*>(val.hash) & 0x0000000000000080ULL) == 0;
}

bool is_canonical_right(const digest_type& val) {
    return (*reinterpret_cast<const uint64_t*>(val.hash) & 0x0000000000000080ULL) != 0;
}

digest_type merkle(vector<digest_type> ids) {
    if (0 == ids.size()) { return digest_type(); }

    while (ids.size() > 1) {
        if (ids.size() % 2)
            ids.push_back(ids.back());

        for (int i = 0; i < ids.size() / 2; i++) {
            ids[i] = sha256(make_canonical_pair(ids[2 * i], ids[(2 * i) + 1]));
        }

        ids.resize(ids.size() / 2);
    }

    return ids.front();
}

}
