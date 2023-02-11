/*
 * AsyncFileChaos.cpp
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2022 Apple Inc. and the FoundationDB project authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fdbrpc/AsyncFileChaos.h"
#include "fdbrpc/simulator.h"

double AsyncFileChaos::getDelay() const {
	double delayFor = 0.0;
	if (!enabled)
		return delayFor;

	auto res = g_network->global(INetwork ::enDiskFailureInjector);
	if (res) {
		DiskFailureInjector* delayInjector = static_cast<DiskFailureInjector*>(res);
		delayFor = delayInjector->getDiskDelay();

		// increment the metric for disk delays
		if (delayFor > 0.0) {
			auto res = g_network->global(INetwork ::enChaosMetrics);
			if (res) {
				ChaosMetrics* chaosMetrics = static_cast<ChaosMetrics*>(res);
				chaosMetrics->diskDelays++;
			}
		}
	}
	return delayFor;
}

Future<int> AsyncFileChaos::read(void* data, int length, int64_t offset) {
	double diskDelay = getDelay();

	if (diskDelay == 0.0)
		return file->read(data, length, offset);

	// Wait for diskDelay before submitting the I/O
	// Template types are being provided explicitly because they can't be automatically deduced for some reason.
	// Capture file by value in case this is destroyed during the delay
	return mapAsync<Void, std ::function<Future<int>(Void)>, int>(
	    delay(diskDelay), [=, file = file](Void _) -> Future<int> { return file->read(data, length, offset); });
}

Future<Void> AsyncFileChaos::write(void const* data, int length, int64_t offset) {
	Arena arena;
	char* pdata = nullptr;
	Optional<uint64_t> corruptedBytePosition;

	// Check if a bit flip event was injected, if so, copy the buffer contents
	// with a random bit flipped in a new buffer and use that for the write
	auto res = g_network->global(INetwork ::enBitFlipper);
	if (enabled && res) {
		auto bitFlipPercentage = static_cast<BitFlipper*>(res)->getBitFlipPercentage();
		if (bitFlipPercentage > 0.0) {
			auto bitFlipProb = bitFlipPercentage / 100;
			if (deterministicRandom()->random01() < bitFlipProb) {
				pdata = (char*)arena.allocate4kAlignedBuffer(length);
				memcpy(pdata, data, length);
				// flip a random bit in the copied buffer
				auto corruptedPos = deterministicRandom()->randomInt(0, length);
				pdata[corruptedPos] ^= (1 << deterministicRandom()->randomInt(0, 8));
				// mark the block as corrupted
				corruptedBytePosition = offset + corruptedPos;
				TraceEvent("CorruptedByteInjection")
				    .detail("Filename", file->getFilename())
				    .detail("Position", corruptedBytePosition)
				    .log();

				// increment the metric for bit flips
				auto res = g_network->global(INetwork ::enChaosMetrics);
				if (res) {
					ChaosMetrics* chaosMetrics = static_cast<ChaosMetrics*>(res);
					chaosMetrics->bitFlips++;
				}
			}
		}
	}

	// Wait for diskDelay before submitting the write
	// Capture file by value in case this is destroyed during the delay
	return mapAsync<Void, std ::function<Future<Void>(Void)>, Void>(
	    delay(getDelay()), [=, file = file](Void _) -> Future<Void> {
		    if (pdata) {
			    return map(holdWhile(arena, file->write(pdata, length, offset)),
			               [corruptedBytePosition, file = file](auto res) {
				               if (g_network->isSimulated() && corruptedBytePosition.present()) {
					               g_simulator->corruptedBytes.mark(file->getFilename(), corruptedBytePosition.get());
				               }
				               return res;
			               });
		    }

		    return file->write(data, length, offset);
	    });
}

Future<Void> AsyncFileChaos::truncate(int64_t size) {
	double diskDelay = getDelay();
	if (diskDelay == 0.0)
		return file->truncate(size);

	// Wait for diskDelay before submitting the I/O
	// Capture file by value in case this is destroyed during the delay
	return mapAsync<Void, std ::function<Future<Void>(Void)>, Void>(
	    delay(diskDelay), [size, file = file](Void _) -> Future<Void> {
		    g_simulator->corruptedBytes.truncate(file->getFilename(), size);
		    return file->truncate(size);
	    });
}

Future<Void> AsyncFileChaos::sync() {
	double diskDelay = getDelay();
	if (diskDelay == 0.0)
		return file->sync();

	// Wait for diskDelay before submitting the I/O
	// Capture file by value in case this is destroyed during the delay
	return mapAsync<Void, std ::function<Future<Void>(Void)>, Void>(
	    delay(diskDelay), [=, file = file](Void _) -> Future<Void> { return file->sync(); });
}

Future<int64_t> AsyncFileChaos::size() const {
	double diskDelay = getDelay();
	if (diskDelay == 0.0)
		return file->size();

	// Wait for diskDelay before submitting the I/O
	// Capture file by value in case this is destroyed during the delay
	return mapAsync<Void, std ::function<Future<int64_t>(Void)>, int64_t>(
	    delay(diskDelay), [=, file = file](Void _) -> Future<int64_t> { return file->size(); });
}