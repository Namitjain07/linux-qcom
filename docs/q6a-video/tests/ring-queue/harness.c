/* Userspace replay of iris_hfi_queue_write() from the kernel source, extracted verbatim.
 * The "firmware" side treats read_idx == write_idx as an empty queue, as the HFI
 * shared-queue protocol does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint32_t u32; typedef uint8_t u8;
#define mb() __sync_synchronize()
#define IFACEQ_MAX_PKT_SIZE 1024
#define IFACEQ_QUEUE_SIZE (IFACEQ_MAX_PKT_SIZE * 8)   /* real value is larger; scaled so the demo is short */
struct iris_hfi_queue_header { u32 queue_type, pkt_size, pkt_drop_cnt, rx_wm, tx_wm, rx_req, tx_req, start_addr, queue_size, read_idx, write_idx; };
struct iris_iface_q_info { struct iris_hfi_queue_header *qhdr; void *kernel_vaddr; };
#define ENOSPC 28
#define EINVAL 22
#include FNFILE   /* the function, copied verbatim from the kernel source */
int main(void){
  static u8 mem[IFACEQ_QUEUE_SIZE]; struct iris_hfi_queue_header h = {0};
  struct iris_iface_q_info q = { &h, mem };
  u8 pkt[1024]; int accepted = 0, lost = 0;
  const u32 psz = 1024;            /* 8 packets exactly tile the 8 KiB demo queue */
  for (int i = 0; i < 10; i++) {
    memset(pkt, 0xA0 + i, psz);
    int r = iris_hfi_queue_write(&q, pkt, psz);
    u32 w = h.write_idx * 4, rd = h.read_idx * 4;
    printf("write #%d: ret=%-3d write_idx=%-5u read_idx=%-3u firmware_sees=%s\n", i, r, w, rd, (w == rd) ? "EMPTY" : "data");
    if (r) break;
    accepted++;
    if (w == rd) lost++;           /* host believes it queued the packet, firmware sees nothing */
  }
  printf("accepted=%d  packets that vanish because the full queue looks empty=%d\n", accepted, lost);
  puts(lost ? "RESULT: BUG - the full queue is reported as success and then looks empty to the firmware"
            : "RESULT: OK - the queue is reported full (-ENOSPC) before the indices collide");
  return lost != 0;
}
