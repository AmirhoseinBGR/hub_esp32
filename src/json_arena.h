/**
 * json_arena.h - bump allocator over a fixed static buffer for ArduinoJson.
 *
 * The hub serializes its state every ~1.2 s forever. A heap JsonDocument
 * mallocs/frees its variant pools on every cycle, which slowly fragments
 * the heap until the TLS handshake can no longer get the large block it
 * needs ("start_ssl_client: -1" on every WSS reconnect; only a power
 * cycle recovered). Documents built on this arena never touch the heap:
 * reset() rewinds the bump pointer and the next document reuses the same
 * bytes. Exhaustion surfaces as JsonDocument::overflowed(), never as a
 * crash. Loop-task only: one live document per arena at a time, and the
 * owning code must reset() only while no document is alive.
 */
#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

template <size_t CAP>
class JsonArena final : public Allocator
{
public:
    void reset() { used_ = 0; }
    size_t used() const { return used_; }
    static constexpr size_t capacity() { return CAP; }

    void *allocate(size_t size) override
    {
        if (size == 0) size = 1;
        const size_t total = kHeader + align_up(size);
        if (used_ + total > CAP) return nullptr;
        uint8_t *raw = buf_ + used_;
        used_ += total;
        memcpy(raw, &size, sizeof(size)); // payload size, for reallocate()
        return raw + kHeader;
    }

    void deallocate(void *) override {} // freed wholesale by reset()

    void *reallocate(void *ptr, size_t new_size) override
    {
        if (!ptr) return allocate(new_size);
        if (new_size == 0) return nullptr;
        size_t old_size;
        memcpy(&old_size, (const uint8_t *)ptr - kHeader, sizeof(old_size));
        if (new_size <= old_size) return ptr;
        void *next = allocate(new_size);
        if (!next) return nullptr;
        memcpy(next, ptr, old_size); // old block is abandoned until reset()
        return next;
    }

private:
    static constexpr size_t align_up(size_t n) { return (n + 15u) & ~static_cast<size_t>(15u); }
    static constexpr size_t kHeader = align_up(sizeof(size_t)); // keeps payloads 16B aligned

    alignas(16) uint8_t buf_[CAP];
    size_t used_ = 0;
};
