#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <hardware/adc.h>
#include <hardware/dma.h>
#include "pico/util/queue.h"
#include <hardware/regs/dreq.h>
#include <stdio.h>
#include <stdint.h>
#include <pico/platform.h>
#include "input_queue.h"
#include "core1.h"

// Some of this code copied lovingly from https://forums.raspberrypi.com/viewtopic.php?t=350378 then edited as needed
// Demo to continuously sample all ADC inputs and write them to a location in
// memory where they can be read. Datasheet calls this data "scattering."

// Note: According to the datasheet sec 2.5.1, DMA read and write addresses must
//  be pointers to an address.
// Note: According to the datasheet sec 2.5.1.1, the way to reinitialize a
//  channel with an incrementing (read or write) address would be to rewrite the
//  starting address before (or upon) restart.
//  Otherwise, "If READ_ADDR and WRITE_ADDR are not reprogrammed, the DMA will
//  use the current values as start addresses for the next transfer."
queue_t queue;

const int QUEUE_LENGTH = 128;

int main()
{
    stdio_usb_init();
    // stdio_set_translate_crlf(&stdio_usb, false); // Don't replace outgoing chars.
    while (!stdio_usb_connected())
    {
    } // Block until connection to serial port.
    sleep_ms(50);
    printf("starting\n");
    queue_init(&queue, sizeof(struct queueItem), QUEUE_LENGTH); // initialize the queue
    printf("Queue Initialized\n");

    multicore_launch_core1(core1_entry);
    printf("Second Core launched\n");
    absolute_time_t print_timer = make_timeout_time_ms(17);
    absolute_time_t sample_timer = make_timeout_time_us(100);
    while (true)
    {
        absolute_time_t current_time = get_absolute_time();
        if (absolute_time_diff_us(print_timer, current_time) > 0)
        {   
            struct queueItem item;
            if (queue_try_remove(&queue, &item))
            {
                // printf(">r_ka:%1.4f\n>r_don:%1.4f\n>l_ka:%1.4f\n>l_don:%1.4f\n",
                //        MAX(0, item.r_ka), MAX(0, item.r_don - item.r_ka),MAX(0,item.l_ka),MAX(0,item.l_don-item.r_don));
            }
            print_timer = make_timeout_time_ms(2);
        }
    }
}
