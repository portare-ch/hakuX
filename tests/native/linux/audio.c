#include "native/dev/apu/apu_int.h"
#include <stdio.h>

void nc_audio_set_paused(bool on);

static void check(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        exit(1);
    }
}

int main(void)
{
    alarm(5);
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
    static uint8_t silence[192000];
    nc_audio_set_paused(true);
    check(apu_out_open(sizeof(silence), 480, &wake) == 1920, "480-frame diagnostic callback");
    apu_out_push(silence, sizeof(silence));
    usleep(50000);
    check(apu_out_queued() == sizeof(silence), "paused sink preserves a full ring");
    pthread_mutex_lock(&lock);
    nc_audio_set_paused(false);
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec++;
    check(pthread_cond_timedwait(&wake, &lock, &deadline) == 0, "full-ring producer receives drain wakeup");
    pthread_mutex_unlock(&lock);
    int before = apu_out_queued();
    usleep(300000);
    nc_audio_set_paused(true);
    int after = apu_out_queued();
    printf("drained %d bytes in approximately 300 ms (expected 57600)\n", before - after);
    check(before - after >= 48000 && before - after <= 67200, "output drains at 48 kHz stereo, not as fast as possible");
    uint32_t cb, short_cb;
    apu_out_stats(&cb, &short_cb);
    check(cb >= 25 && short_cb == 0, "buffered output has no short callbacks");
    usleep(100000);
    check(apu_out_queued() == after, "pause acknowledges before returning");
    apu_out_stats(&cb, &short_cb);
    check(cb == 0, "no callbacks during pause");
    nc_pause_set(true);
    nc_audio_set_paused(false);
    usleep(50000);
    check(apu_out_queued() == after, "core pause also preserves queued audio");
    nc_pause_set(false);
    usleep(50000);
    check(apu_out_queued() < after, "audio resumes after core pause");
    return 0;
}
