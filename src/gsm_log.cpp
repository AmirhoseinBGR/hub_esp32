/** gsm_log.cpp - small ring buffer; web + USB serial read the same stream. */
#include "gsm_log.h"
#include "config.h"

#define GSM_LOG_LINES 60
#define GSM_LOG_LEN 176

struct LogLine
{
    uint32_t seq = 0;
    char text[GSM_LOG_LEN];
};

static LogLine ring[GSM_LOG_LINES];
static uint8_t head = 0;       // next write slot
static uint32_t seq = 0;
static bool mirror = true;

// Strip ANSI CSI sequences (ESC [ params final)
static void strip_ansi(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < cap; p++)
    {
        if (*p == 0x1B)
        {
            const char *q = p + 1;
            if (*q == '[')
            {
                q++;
                while (*q && (unsigned char)*q >= 0x20 && (unsigned char)*q <= 0x3F) q++;
                if (*q && (unsigned char)*q >= 0x40 && (unsigned char)*q <= 0x7E) q++;
            }
            else if (*q) q++;
            p = q - 1;
            continue;
        }
        out[o++] = *p;
    }
    out[o] = '\0';
}

void gsm_log_add(const char *text)
{
    if (!text || !*text) return;

    LogLine &l = ring[head];
    l.seq = ++seq;
    strip_ansi(text, l.text, sizeof(l.text));
    head = (head + 1) % GSM_LOG_LINES;

    if (mirror) Serial.printf("[GSM] %s\n", l.text);
}

uint32_t gsm_log_latest_seq() { return seq; }

void gsm_log_to_json(JsonArray arr, uint32_t after_seq, int max_lines)
{
    // oldest first, keep the newest <max_lines> entries after <after_seq>
    int first[GSM_LOG_LINES];
    int n = 0;

    for (int i = 0; i < GSM_LOG_LINES; i++)
        if (ring[i].seq > after_seq) first[n++] = i;

    int skip = n - max_lines;
    if (skip < 0) skip = 0;

    for (int k = skip; k < n; k++)
    {
        const LogLine &l = ring[first[k]];
        JsonObject o = arr.add<JsonObject>();
        o["seq"] = l.seq;
        o["t"] = l.text;
    }
}

void gsm_log_mirror_serial(bool on) { mirror = on; }
bool gsm_log_mirroring() { return mirror; }
