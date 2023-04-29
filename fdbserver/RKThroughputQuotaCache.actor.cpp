/**
 * RKThroughputQuotaCache.actor.cpp
 */

#include "fdbclient/ReadYourWrites.h"
#include "fdbserver/IRKThroughputQuotaCache.h"
#include "flow/actorcompiler.h" // must be last include

class RKThroughputQuotaCacheImpl {
public:
	ACTOR static Future<Void> run(RKThroughputQuotaCache* self) {
		state std::unordered_set<TransactionTag> tagsWithQuota;

		loop {
			state ReadYourWritesTransaction tr(self->db);
			loop {
				try {
					tr.setOption(FDBTransactionOptions::ACCESS_SYSTEM_KEYS);
					tr.setOption(FDBTransactionOptions::PRIORITY_SYSTEM_IMMEDIATE);
					tr.setOption(FDBTransactionOptions::READ_LOCK_AWARE);

					tagsWithQuota.clear();
					state RangeResult currentQuotas = wait(tr.getRange(tagQuotaKeys, CLIENT_KNOBS->TOO_MANY));
					TraceEvent("GlobalTagThrottler_ReadCurrentQuotas", self->id).detail("Size", currentQuotas.size());
					self->quotas.clear();
					for (auto const kv : currentQuotas) {
						auto const tag = kv.key.removePrefix(tagQuotaPrefix);
						self->quotas[tag] = ThrottleApi::TagQuotaValue::unpack(Tuple::unpack(kv.value));
					}
					wait(delay(5.0));
					break;
				} catch (Error& e) {
					TraceEvent("GlobalTagThrottler_MonitoringChangesError", self->id).error(e);
					wait(tr.onError(e));
				}
			}
		}
	}
}; // class RKThroughputQuotaCacheImpl

RKThroughputQuotaCache::RKThroughputQuotaCache(UID id, Database db) : id(id), db(db) {}

RKThroughputQuotaCache::~RKThroughputQuotaCache() = default;

Optional<int64_t> RKThroughputQuotaCache::getTotalQuota(TransactionTag const& tag) const {
	auto it = quotas.find(tag);
	if (it == quotas.end()) {
		return {};
	} else {
		return it->second.totalQuota;
	}
}

Optional<int64_t> RKThroughputQuotaCache::getReservedQuota(TransactionTag const& tag) const {
	auto it = quotas.find(tag);
	if (it == quotas.end()) {
		return {};
	} else {
		return it->second.reservedQuota;
	}
}

int RKThroughputQuotaCache::size() const {
	return quotas.size();
}

Future<Void> RKThroughputQuotaCache::run() {
	return RKThroughputQuotaCacheImpl::run(this);
}

MockRKThroughputQuotaCache::~MockRKThroughputQuotaCache() = default;

Optional<int64_t> MockRKThroughputQuotaCache::getTotalQuota(TransactionTag const& tag) const {
	auto it = quotas.find(tag);
	if (it == quotas.end()) {
		return {};
	} else {
		return it->second.totalQuota;
	}
}

Optional<int64_t> MockRKThroughputQuotaCache::getReservedQuota(TransactionTag const& tag) const {
	auto it = quotas.find(tag);
	if (it == quotas.end()) {
		return {};
	} else {
		return it->second.reservedQuota;
	}
}

int MockRKThroughputQuotaCache::size() const {
	return quotas.size();
}

void MockRKThroughputQuotaCache::setQuota(TransactionTag const& tag, int64_t totalQuota, int64_t reservedQuota) {
	quotas[tag].totalQuota = totalQuota;
	quotas[tag].reservedQuota = reservedQuota;
}

void MockRKThroughputQuotaCache::removeQuota(TransactionTag const& tag) {
	quotas.erase(tag);
}

Future<Void> MockRKThroughputQuotaCache::run() {
	return Never();
}
