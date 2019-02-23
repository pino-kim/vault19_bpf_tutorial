#include <linux/ptrace.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

struct called_info {
        u64 start;
        u64 end;
};

struct bpf_map_def SEC("maps") called_info_map = {
        .type = BPF_MAP_TYPE_HASH,
        .key_size = sizeof(long),
        .value_size = sizeof(struct called_info),
        .max_entries = 4096,
};
/* max_latency_info[0]: key == bio_ptr,
 * max_latency_info[1]: val == max_time_duration
 */
struct bpf_map_def SEC("maps") max_latency_info = {
        .type = BPF_MAP_TYPE_ARRAY,
        .key_size = sizeof(u32),
        .value_size = sizeof(u64),
        .max_entries = 2,
};

SEC("kprobe/submit_bio")
int submit_bio_entry(struct pt_regs *ctx)
{
        char fmt[] = "submit_bio(bio=0x%lx) called: %llu\n";
        u64 start_time = bpf_ktime_get_ns();
        long bio_ptr = PT_REGS_PARM1(ctx);
        struct called_info called_info = {
                .start = start_time,
                .end = 0
        };

        bpf_map_update_elem(&called_info_map, &bio_ptr, &called_info, BPF_ANY);
        bpf_trace_printk(fmt, sizeof(fmt), bio_ptr, start_time);
        return 0;
}

SEC("kprobe/bio_endio")
int bio_endio_entry(struct pt_regs *ctx)
{
        char fmt2[] = "submit_bio() -> bio_endio() time duration: %llu ns\n\n";
        char fmt[] = "bio_endio (bio=0x%lx) called: %llu\n";
        u64 end_time = bpf_ktime_get_ns();
        long bio_ptr = PT_REGS_PARM1(ctx);
        struct called_info *called_info;
        u32 key_idx = 0, val_idx = 1;
        u64 *max_time_duration;
        u64 time_duration;

        called_info = bpf_map_lookup_elem(&called_info_map, &bio_ptr);
        if (!called_info)
                return 0;

        called_info->end = end_time;
        time_duration = called_info->end - called_info->start;

        bpf_trace_printk(fmt, sizeof(fmt), bio_ptr, end_time);
        bpf_trace_printk(fmt2, sizeof(fmt2), time_duration);

        max_time_duration = bpf_map_lookup_elem(&max_latency_info, &val_idx);

        if (max_time_duration && (time_duration <= *max_time_duration))
                return 0;

        bpf_map_update_elem(&max_latency_info, &key_idx, &bio_ptr, BPF_ANY);
        bpf_map_update_elem(&max_latency_info, &val_idx, &time_duration, BPF_ANY);

        return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
